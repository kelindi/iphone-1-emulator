#include "soc.h"
#include <stdio.h>
#include <string.h>

static uint16_t sum16(const uint8_t *p, unsigned n) {
    uint32_t sum = 0;
    for (unsigned i = 0; i < n; i++) sum += p[i];
    return (uint16_t)sum;
}

static const struct {
    uint8_t id, len;
} Z1_PARAMS[] = {
    { 0x40u, 1u }, { 0x41u, 1u }, { 0x4fu, 1u }, { 0x68u, 1u },
    { 0x70u, 1u }, { 0xa0u, 1u }, { 0xa3u, 1u }, { 0xa4u, 1u },
    { 0xa5u, 1u }, { 0xa8u, 1u }, { 0xadu, 1u }, { 0xafu, 1u },
    { 0xb0u, 1u }, { 0xd7u, 1u },
    { 0xaeu, 2u },
    { 0x7fu, 4u }, { 0xb4u, 4u }, { 0xb6u, 4u },
    { 0xbfu, 4u }, { 0xcbu, 4u },
    { 0xb1u, 6u }, { 0xb2u, 6u },
    { 0xabu, 1u }
};

static bool z1_param(uint8_t id, unsigned *offset, unsigned *len) {
    unsigned at = 0u;
    for (unsigned i = 0; i < sizeof Z1_PARAMS / sizeof Z1_PARAMS[0]; i++) {
        if (Z1_PARAMS[i].id == id) {
            if (offset) *offset = at;
            if (len) *len = Z1_PARAMS[i].len;
            return true;
        }
        at += Z1_PARAMS[i].len;
    }
    return false;
}

static void reset_protocol(s5l_mtz1_t *d) {
    d->firmware = false;
    d->stage_b = false;
    d->check_seen = false;
    d->atn = false;
    d->atn_pulse = false;
    d->operating_mode = 0;
    memset(d->params, 0, sizeof d->params);
    d->last_checksum = 0;
    d->b_checksum = 0;
    d->pending_len = 0;
    d->last_n = 0;
    d->frame_seq = 0;
    d->frame_ms = 0;
    d->uptime_ms = 0;
    d->uptime_tick_acc = 0;
    memset(d->held, 0, sizeof d->held);
    d->held_n = 0;
    d->lift_pending = false;
    d->tick_acc = 0;
    d->pkt_len = 0;
    d->rx_len = 0;
    d->transaction_overflow = false;
    d->raw_stream = false;
    d->aspeed_stream = false;
}

void s5l_mtz1_reset(s5l_mtz1_t *dev) {
    bool trace = dev && dev->trace;
    if (!dev) return;
    memset(dev, 0, sizeof *dev);
    dev->trace = trace;
    dev->geom.family_id = 1u;
    dev->geom.endianness = 1u;
    dev->geom.rows = 15u;
    dev->geom.columns = 10u;
    dev->geom.bcd_version = 0x0100u;
    dev->geom.surface_width = 4800u;
    dev->geom.surface_height = 7200u;
    reset_protocol(dev);
}

static unsigned z1_report(const s5l_mtz1_t *d, uint8_t id, uint8_t *out) {
    unsigned offset, len;
    unsigned n = s5l_mt_report(&d->geom, id, out);
    if (n && id != 0xd7u) {
        if (id == 0xabu) out[0] = d->operating_mode;
        return n;
    }
    if (!z1_param(id, &offset, &len)) return 0u;
    memcpy(out, d->params + offset, len);
    if (id == 0xabu) out[0] = d->operating_mode;
    return len;
}

static uint8_t firmware_byte(s5l_mtz1_t *d, unsigned pos,
                             uint8_t out) {
    uint8_t id = d->rx_len > 1u ? d->rx[1] : 0u;
    uint8_t body[MTZ2_REPORT_MAX];
    unsigned n;
    (void)out;
    if (pos == 0u) return 0xaau;
    switch (d->rx[0]) {
    case 0xd0u:
        return pos == 1u ? 1u : pos == 2u ? 2u : 0u;
    case 0x8fu:
        n = z1_report(d, id, body);
        if (!n) d->report_unknown++;
        if (pos == 4u) return n ? (uint8_t)(n >> 8) : 0x10u;
        if (pos == 5u) return (uint8_t)n;
        if (pos == 6u) return (uint8_t)(((uint16_t)id +
                                         (n ? n >> 8 : 0x10u) + n) >> 8);
        if (pos == 7u) return (uint8_t)((uint16_t)id +
                                        (n ? n >> 8 : 0x10u) + n);
        return 0u;
    case 0x82u:
        n = z1_report(d, id, body);
        if (!n) d->report_unknown++;
        if (id == 0xabu && n) body[0] = d->operating_mode;
        if (pos >= 4u && pos < n + 4u) return body[pos - 4u];
        if (pos == n + 4u || pos == n + 5u) {
            uint16_t ck = (uint16_t)(id + sum16(body, n));
            return pos == n + 4u ? (uint8_t)(ck >> 8) : (uint8_t)ck;
        }
        return 0u;
    case 0x92u:
        n = z1_report(d, id, body);
        if (n && (pos == n + 4u || pos == n + 5u)) {
            uint16_t ck = (uint16_t)(id + sum16(d->rx + 4u, n));
            return pos == n + 4u ? (uint8_t)(ck >> 8) : (uint8_t)ck;
        }
        return 0u;
    case 0x46u:
    case 0x64u:
    case 0x65u: {
        unsigned l = d->pending_len ? d->pending_len + 2u : 0u;
        if (pos == 1u) return (uint8_t)(l >> 8);
        if (pos == 2u) return (uint8_t)l;
        if (pos == 3u) {
            uint16_t ck = (uint16_t)((l >> 8) + (uint8_t)l);
            return (uint8_t)(ck >> 8);
        }
        if (pos == 4u) {
            uint16_t ck = (uint16_t)((l >> 8) + (uint8_t)l);
            return (uint8_t)ck;
        }
        return 0u;
    }
    case 0x47u:
    default: {
        unsigned l = d->pending_len + 2u;
        if (d->pending_len && pos >= 1u && pos < d->pending_len + 1u)
            return d->pending[pos - 1u];
        if (d->pending_len && (pos == l - 1u || pos == l)) {
            uint16_t ck = sum16(d->pending, d->pending_len);
            return pos == l - 1u ? (uint8_t)(ck >> 8) : (uint8_t)ck;
        }
        return 0u;
    }
    }
}

static uint8_t boot_byte(const s5l_mtz1_t *d, unsigned pos) {
    if (pos == 0u) return 0xd0u;
    if (d->rx_len >= 1u && d->rx[0] == 0x05u && (pos == 2u || pos == 3u))
        return pos == 2u ? (uint8_t)(d->last_checksum >> 8)
                         : (uint8_t)d->last_checksum;
    return 0u;
}

static void finish_transaction(s5l_mtz1_t *d) {
    uint8_t op = d->rx_len ? d->rx[0] : 0u;
    d->transactions++;
    if (d->firmware) {
        if (op == 0x92u && d->rx_len >= 5u) {
            uint8_t id = d->rx[1];
            uint8_t body[MTZ2_REPORT_MAX];
            unsigned n = z1_report(d, id, body);
            unsigned offset;
            if (n && d->rx_len >= n + 4u &&
                z1_param(id, &offset, 0)) {
                memcpy(d->params + offset, d->rx + 4u, n);
                if (id == 0xabu) d->operating_mode = d->rx[4];
            }
        }
        if (op != 0x46u && op != 0x64u && op != 0x65u) {
            if (d->pending_len) {
                d->pending_len = 0u;
                d->frames_read++;
            }
            d->atn = false;
        } else {
            d->atn_pulse = false;
            if (!d->pending_len) d->atn = false;
        }
        if (d->trace)
            fprintf(stderr, "[mtz1] firmware op=%02x id=%02x len=%u pending=%u\n",
                    op, d->rx_len > 1u ? d->rx[1] : 0u,
                    d->rx_len, d->pending_len);
        return;
    }
    if (op == 0xc2u) {
        if (d->rx_len == 4u && d->rx[1] == 0u &&
            d->rx[2] == 0u && d->rx[3] == 0u) {
            d->stage_b = true;
            d->b_checksum = 0u;
            d->check_seen = false;
            d->aspeed_stream = false;
            d->rx_len = 0u;
            if (d->trace) fprintf(stderr, "[mtz1] blank upload reset\n");
            return;
        }
        if (d->rx_len >= 6u) {
            if (d->rx_len < 1024u) {
                /*
                 * AppleMultitouchSPI sends the 16-byte A-speed prefix and
                 * the remaining bytes as separate chip-select transactions.
                 * Keep the upload accumulator alive for that continuation;
                 * a complete packet still follows the documented 1024-byte
                 * path used by the unit tests.
                 */
                d->last_checksum = sum16(d->rx, d->rx_len);
                d->aspeed_stream = true;
            } else {
                d->last_checksum = sum16(d->rx, 1022u);
                d->aspeed_stream = false;
            }
            d->aspeed_bytes += d->rx_len - 6u;
            if (d->trace)
                fprintf(stderr, "[mtz1] aspeed bytes=%llu checksum=%04x\n",
                        (unsigned long long)d->aspeed_bytes,
                        d->last_checksum);
        }
    } else if (op == 0xc3u) {
        d->stage_b = true;
        d->b_checksum = 0u;
        d->check_seen = false;
        if (d->trace) fprintf(stderr, "[mtz1] mode=stage-b\n");
    } else if (op == 0x05u && d->rx_len >= 4u && d->stage_b) {
        d->last_checksum = (uint16_t)d->b_checksum;
        d->check_seen = true;
        d->checks++;
        if (d->trace)
            fprintf(stderr, "[mtz1] check checksum=%04x bytes=%llu\n",
                    d->last_checksum, (unsigned long long)d->bspeed_bytes);
    } else if (op == 0xc4u && d->rx_len >= 4u &&
               d->rx[1] == 0u && d->rx[2] == 0u && d->rx[3] == 0xc4u) {
        d->executes++;
        if (!d->stage_b) {
            /*
             * Apple's 1A543a path executes stage A and then sends the blank
             * c2 boundary directly; it does not issue the documented c3
             * command. Treat that observed transition as stage B while still
             * retaining the explicit c3 path used by the protocol tests.
             */
            d->stage_b = true;
            d->b_checksum = 0u;
            d->check_seen = false;
            if (d->trace) fprintf(stderr, "[mtz1] mode=stage-b\n");
        } else if (d->check_seen || d->rx_len == 4u) {
            d->firmware = true;
            d->stage_b = false;
            d->mode_transitions++;
            d->atn = true;
            d->atn_pulse = true;
            if (d->trace)
                fprintf(stderr, "[mtz1] mode=firmware transitions=%llu\n",
                        (unsigned long long)d->mode_transitions);
        }
    } else if (d->stage_b && op != 0x05u && op != 0xc2u &&
               op != 0xc3u && op != 0xc4u) {
        /* The stream checksum is accumulated byte-by-byte in transfer(). */
    } else if (d->aspeed_stream && op != 0x05u && op != 0xc2u &&
               op != 0xc3u && op != 0xc4u) {
        d->aspeed_stream = false;
        if (d->trace)
            fprintf(stderr, "[mtz1] aspeed continuation bytes=%u checksum=%04x\n",
                    d->rx_len, d->last_checksum);
    } else if (op != 0x05u && op != 0xc2u && op != 0xc3u &&
               op != 0xc4u) {
        d->unknown_ops++;
    }
}

static uint8_t mtz1_transfer(void *ctx, uint8_t out) {
    s5l_mtz1_t *d = ctx;
    unsigned pos;
    if (!d) return 0u;
    pos = d->rx_len;
    if (pos == 0u && (d->stage_b || d->aspeed_stream) &&
        out != 0x05u && out != 0xc2u && out != 0xc3u && out != 0xc4u)
        d->raw_stream = true;
    if (d->raw_stream) {
        if (d->stage_b) {
            d->b_checksum += out;
            d->bspeed_bytes++;
        } else if (d->aspeed_stream) {
            d->last_checksum += out;
            d->aspeed_bytes++;
        }
    }
    if (pos < sizeof d->rx) d->rx[pos] = out;
    else d->transaction_overflow = true;
    d->rx_len++;
    d->pkt_len++;
    d->octets++;
    if (d->trace && pos == 0u)
        fprintf(stderr, "[mtz1] %s op=%02x\n",
                d->firmware ? "firmware" : "boot", out);
    return d->firmware ? firmware_byte(d, pos, out) : boot_byte(d, pos);
}

void s5l_mtz1_bind(s5l_mtz1_t *dev, s5l_spi_slave_t *slave) {
    if (!dev || !slave) return;
    dev->present = true;
    memset(slave, 0, sizeof *slave);
    slave->ctx = dev;
    slave->transfer = mtz1_transfer;
}

void s5l_mtz1_select_pin(void *ctx, bool level) {
    s5l_mtz1_t *d = ctx;
    bool selected;
    if (!d) return;
    selected = !level;
    if (selected == d->selected) return;
    d->selected = selected;
    if (selected) {
        /*
         * On the real boot path GPIO starts low, so the first 16-byte
         * A-speed prefix is clocked before our watcher sees its first
         * active-select edge. The following 1008-byte DMA continuation
         * arrives after that edge; retain the prefix and let the final
         * deselect complete the 1024-byte packet.
         */
        if (!(d->rx_len >= 6u && d->rx[0] == 0xc2u &&
              d->rx_len < 1024u)) {
            d->rx_len = 0u;
            d->pkt_len = 0u;
            d->transaction_overflow = false;
            d->raw_stream = false;
        } else {
            d->aspeed_stream = true;
        }
        if (d->trace) fprintf(stderr, "[mtz1] select\n");
    } else if (d->rx_len) {
        if (d->trace)
            fprintf(stderr, "[mtz1] deselect len=%u aspeed=%u stageb=%u\n",
                    d->rx_len, d->aspeed_stream, d->stage_b);
        finish_transaction(d);
        d->pkt_len = 0u;
    }
}

void s5l_mtz1_power_pin(void *ctx, bool level) {
    s5l_mtz1_t *d = ctx;
    if (!d) return;
    if (d->trace) fprintf(stderr, "[mtz1] power_ldo=%u\n", level);
    if (level != d->powered) {
        d->powered = level;
        reset_protocol(d);
    }
}

bool s5l_mtz1_irq(const s5l_mtz1_t *dev) {
    return dev && dev->atn;
}

bool s5l_mtz1_set_contacts(s5l_mtz1_t *d, const s5l_mt_contact_t *c,
                           unsigned n) {
    s5l_mt_contact_t first[MTZ2_CONTACT_MAX];
    unsigned len;
    if (!d || !d->firmware) return false;
    if (!n) {
        if (!d->held_n) return false;
        d->lift_pending = true;
        return true;
    }
    if (!c || n > MTZ2_CONTACT_MAX) return false;
    if (d->pending_len) return false;
    memcpy(first, c, n * sizeof first[0]);
    for (unsigned i = 0; i < n; i++) first[i].phase = MTZ2_PHASE_MAKE_TOUCH;
    uint32_t stamp = d->uptime_ms;
    if (d->frames_queued && stamp <= d->frame_ms) stamp = d->frame_ms + 1u;
    len = s5l_mt_frame_encode_ce(&d->geom, first, n,
                                 (uint8_t)(d->frame_seq + 1u),
                                 stamp, d->pending);
    if (!len) return false;
    memcpy(d->held, first, n * sizeof d->held[0]);
    d->held_n = (uint8_t)n;
    d->lift_pending = false;
    d->tick_acc = 0u;
    memcpy(d->last_contacts, first, n * sizeof d->last_contacts[0]);
    d->last_n = (uint8_t)n;
    d->pending_len = (uint8_t)len;
    d->frame_seq++;
    d->frame_ms = stamp;
    d->frames_queued++;
    d->atn = true;
    return true;
}

static bool queue_held_frame(s5l_mtz1_t *d, uint8_t phase,
                             bool clear_pressure) {
    s5l_mt_contact_t frame[MTZ2_CONTACT_MAX];
    unsigned len;
    if (!d || !d->held_n || d->pending_len) return false;
    memcpy(frame, d->held, d->held_n * sizeof frame[0]);
    for (unsigned i = 0; i < d->held_n; i++) {
        frame[i].phase = phase;
        if (clear_pressure) frame[i].pressure = 0u;
    }
    uint32_t stamp = d->uptime_ms;
    if (d->frames_queued && stamp <= d->frame_ms) stamp = d->frame_ms + 1u;
    len = s5l_mt_frame_encode_ce(&d->geom, frame, d->held_n,
                                 (uint8_t)(d->frame_seq + 1u),
                                 stamp,
                                 d->pending);
    if (!len) return false;
    memcpy(d->last_contacts, frame, d->held_n * sizeof d->last_contacts[0]);
    d->last_n = d->held_n;
    d->pending_len = (uint8_t)len;
    d->frame_seq++;
    d->frame_ms = stamp;
    d->frames_queued++;
    d->atn = true;
    return true;
}

void s5l_mtz1_tick(s5l_mtz1_t *d, uint32_t tb_ticks) {
    if (!d) return;
    if (!d->firmware) {
        d->tick_acc = 0u;
        d->uptime_tick_acc = 0u;
        return;
    }
    uint64_t uptime = (uint64_t)d->uptime_tick_acc + tb_ticks;
    d->uptime_ms += (uint32_t)(uptime / MTZ1_TICKS_PER_MS);
    d->uptime_tick_acc = (uint32_t)(uptime % MTZ1_TICKS_PER_MS);
    if (!d->held_n && !d->lift_pending) {
        d->tick_acc = 0u;
        return;
    }
    uint64_t total = (uint64_t)d->tick_acc + tb_ticks;
    if (total < MTZ1_FRAME_TICKS) {
        d->tick_acc = (uint32_t)total;
        return;
    }
    d->tick_acc = 0u;
    if (d->pending_len) return;
    if (d->lift_pending) {
        if (queue_held_frame(d, MTZ2_PHASE_BREAK_TOUCH, true)) {
            d->held_n = 0u;
            d->lift_pending = false;
        }
    } else if (d->held_n) {
        (void)queue_held_frame(d, MTZ2_PHASE_TOUCHING, false);
    }
}
