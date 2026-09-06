/*
 * S5LBox — Samsung S5L8900 I2C controller.
 *
 * The stock AppleS5L8900XI2CController uses 32-bit accesses.  A transfer is
 * intentionally instantaneous at this abstraction boundary: BUSY therefore
 * always reads zero, and START/RESUME/STOP latch the corresponding interrupt
 * before the guest can next observe the controller.  The interrupt remains a
 * level until the guest clears every pending bit through the W1C register.
 */
#include "soc.h"
#include <string.h>

void s5l_i2c_reset(s5l_i2c_t *bus) {
    if (!bus) return;
    /* Do not preserve callbacks here. Besides making reset semantics explicit,
     * total initialization makes this safe for stack objects containing any
     * prior byte pattern. Board wiring is attached after reset. */
    memset(bus, 0, sizeof *bus);
    bus->sel = -1;
}

bool s5l_i2c_attach(s5l_i2c_t *bus, const s5l_i2c_slave_t *slave) {
    if (!bus || !slave || !slave->start ||
        slave->addr > 0x7fu || bus->slave_count >= S5L_I2C_SLAVES)
        return false;
    for (unsigned i = 0; i < bus->slave_count; i++)
        if (bus->slaves[i].addr == slave->addr) return false;
    bus->slaves[bus->slave_count++] = *slave;
    return true;
}

static int find_slave(const s5l_i2c_t *bus, uint8_t addr) {
    for (unsigned i = 0; i < bus->slave_count; i++)
        if (bus->slaves[i].addr == addr) return (int)i;
    return -1;
}

static void note_unknown(s5l_i2c_t *bus, uint32_t off) {
    for (unsigned i = 0; i < bus->unknown_off_count; i++)
        if (bus->unknown_off[i] == off) return;
    if (bus->unknown_off_count < S5L_I2C_UNKNOWN_OFF)
        bus->unknown_off[bus->unknown_off_count++] = off;
}

static void do_stop(s5l_i2c_t *bus) {
    if (!bus->active) return;
    if (bus->sel >= 0 && (unsigned)bus->sel < bus->slave_count) {
        s5l_i2c_slave_t *slave = &bus->slaves[bus->sel];
        if (slave->stop) slave->stop(slave->ctx);
    }
    bus->active = false;
    bus->reading = false;
    bus->sel = -1;
    bus->intstat |= I2C_INT_STOP;
}

static void do_start(s5l_i2c_t *bus) {
    /* A repeated START ends the slave-local phase, but is not a bus STOP and
     * therefore must not manufacture I2C_INT_STOP. */
    if (bus->active && bus->sel >= 0 &&
        (unsigned)bus->sel < bus->slave_count) {
        s5l_i2c_slave_t *old = &bus->slaves[bus->sel];
        if (old->stop) old->stop(old->ctx);
    }

    bus->reading = (bus->stat & I2C_STAT_MODE) == I2C_STAT_MODE_MRX;
    bus->sel = find_slave(bus, (uint8_t)((bus->ds >> 1) & 0x7fu));
    bus->active = true;
    bus->starts++;

    bool ack = false;
    if (bus->sel >= 0) {
        s5l_i2c_slave_t *slave = &bus->slaves[bus->sel];
        ack = slave->start(slave->ctx, bus->reading);
    }
    bus->nak = !ack;
    if (bus->nak) bus->naks++;

    /* A NAK is still a completed address phase. The driver must receive this
     * interrupt so it can inspect STAT.NAK and return an error instead of
     * sleeping forever. */
    bus->intstat |= I2C_INT_BYTE;
}

static void do_resume(s5l_i2c_t *bus) {
    if (!bus->active) return;

    if (bus->reading) {
        uint8_t val = 0;
        bool was_nak = bus->nak;
        if (bus->sel >= 0 && (unsigned)bus->sel < bus->slave_count &&
            !bus->nak) {
            s5l_i2c_slave_t *slave = &bus->slaves[bus->sel];
            if (slave->read) val = slave->read(slave->ctx);
            else bus->nak = true;
        } else if (!bus->nak) bus->nak = true;
        if (bus->nak && !was_nak) bus->naks++;
        bus->ds = val;
        bus->bytes_rx++;
    } else {
        bool ack = false;
        if (bus->sel >= 0 && (unsigned)bus->sel < bus->slave_count &&
            !bus->nak) {
            s5l_i2c_slave_t *slave = &bus->slaves[bus->sel];
            if (slave->write)
                ack = slave->write(slave->ctx, (uint8_t)bus->ds);
        }
        bus->nak = !ack;
        if (bus->nak) bus->naks++;
        bus->bytes_tx++;
    }
    bus->intstat |= I2C_INT_BYTE;
}

uint32_t s5l_i2c_read(s5l_i2c_t *bus, uint32_t off) {
    if (!bus) return 0;
    switch (off) {
        case I2C_CON:    return bus->con;
        case I2C_STAT:   return (bus->stat & ~(uint32_t)I2C_STAT_NAK) |
                                (bus->nak ? I2C_STAT_NAK : 0u);
        case I2C_ADD:    return bus->add;
        case I2C_DS:     return bus->ds;
        case I2C_BUSY:   return 0u;
        case I2C_ENABLE: return bus->enable;
        case I2C_INT:    return bus->intstat;
        default:
            bus->unknown_reads++;
            note_unknown(bus, off);
            return 0u;
    }
}

void s5l_i2c_write(s5l_i2c_t *bus, uint32_t off, uint32_t val) {
    if (!bus) return;
    switch (off) {
        case I2C_CON:
            bus->con = val;
            /* RESUME is a command on every write, not an edge-triggered
             * stored latch. Consecutive bytes therefore use consecutive
             * writes with bit 4 still set. */
            if (val & I2C_CON_RESUME) do_resume(bus);
            break;
        case I2C_STAT: {
            bool was_start = (bus->stat & I2C_STAT_START) != 0;
            bool now_start = (val & I2C_STAT_START) != 0;
            bus->stat = val & ~(uint32_t)I2C_STAT_NAK;
            if (!was_start && now_start) do_start(bus);
            else if (was_start && !now_start) do_stop(bus);
            break;
        }
        case I2C_ADD:
            bus->add = val;
            break;
        case I2C_DS:
            bus->ds = val & 0xffu;
            break;
        case I2C_BUSY:
            /* Read-only. A guest write cannot wedge the driver's busy poll. */
            break;
        case I2C_ENABLE:
            /* The stock driver writes this around controller lifetime. Its
             * effect on the internal state is not observable in the decoded
             * transfer path, so preserve it without inventing side effects. */
            bus->enable = val;
            break;
        case I2C_INT:
            bus->intstat &= ~val;
            break;
        default:
            bus->unknown_writes++;
            note_unknown(bus, off);
            break;
    }
}

bool s5l_i2c_irq(const s5l_i2c_t *bus) {
    /* INT is a level latch. ENABLE is retained as observed storage; the stock
     * transfer path enables the block before generating any event, and there
     * is no evidence that it masks the external line independently. */
    return bus && bus->intstat != 0u;
}
