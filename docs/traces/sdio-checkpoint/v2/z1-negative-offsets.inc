struct {size_t offset;unsigned size;uint64_t value;} mutations[]={
 {0,1,2}, /*present*/
 {1380,1,177}, /*pending_len*/
 {13,1,6}, /*last_n*/
 {1476,1,6}, /*held_n*/
 {1478,4,60000}, /*tick_acc*/
 {1486,4,6000}, /*uptime_tick_acc*/
 {1087,4,9}, /*pkt_len*/
 {1091,4,1025}, /*rx_len*/
 {10,1,1}, /*transaction_overflow*/
 {1381,1,0}, /*last_contacts[0].id*/
 {1382,1,8}, /*last_contacts[0].phase*/
 {1383,2,320}, /*last_contacts[0].x*/
 {1385,2,480}, /*last_contacts[0].y*/
 {1390,1,1}, /*last_contacts[1].id*/
 {1426,1,0}, /*held[0].id*/
 {1427,1,8}, /*held[0].phase*/
 {1428,2,320}, /*held[0].x*/
 {1430,2,480}, /*held[0].y*/
 {1435,1,1}, /*held[1].id*/
 {1426,1,6}, /*held[0].id id beyond active bound*/
 {1381,1,6}, /*last_contacts[0].id id beyond active bound*/
};
