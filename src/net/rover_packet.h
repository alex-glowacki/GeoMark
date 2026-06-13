/**
 * @file net/rover_packet.h
 * @brief Wire format for the pole-top → handheld status stream.
 *
 * The Zero 2 W (pole-top) streams RoverStatusPacket at 2 Hz over TCP
 * port 4500 to the Pi 5 (handheld).  The magic word lets the client
 * re-sync if it connects mid-stream or after a brief disconnect.
 *
 * Total size: 44 bytes (verified by static_assert below).
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *    0       4    magic        (0x474D524B = "GMRK")
 *    4       8    lat          double, decimal degrees, +N
 *   12       8    lon          double, decimal degrees, +E
 *   20       8    alt_msl      double, metres
 *   28       8    hdop         double
 *   36       1    fix_quality  0=invalid 1=GPS 2=DGPS 4=RTK_fixed 5=Float
 *   37       1    num_sats
 *   38       2    age_of_fix_s
 *   40       1    valid        1 if position fields are meaningful
 *   41       3    _pad         explicit padding to 44 bytes
 */

#ifndef GEOMARK_ROVER_PACKET_H
#define GEOMARK_ROVER_PACKET_H

#include <stddef.h>
#include <stdint.h>

#define ROVER_PACKET_MAGIC 0x474D524Bu /* "GMRK" in little-endian */
#define ROVER_PACKET_PORT 4500

typedef struct {
    uint32_t magic; /* always ROVER_PACKET_MAGIC */
    double lat;     /* decimal degrees, +N */
    double lon;     /* decimal degrees, +E */
    double alt_msl; /* meters MSL */
    double hdop;
    uint8_t fix_quality; /* 0=invalid 1=GPS 2=DGPS 4=RTK 5=Float */
    uint8_t num_sats;
    uint16_t age_of_fix_s;
    uint8_t valid;   /* 1 if position fields are meaningful */
    uint8_t _pad[3]; /* explicit padding → 44 bytes total */
} __attribute__((packed)) RoverStatusPacket;

/* Catch layout regressions at compile time */
_Static_assert(sizeof(RoverStatusPacket) == 44, "RoverStatusPacket must be exactly 44 bytes");

#endif /* GEOMARK_ROVER_PACKET_H */