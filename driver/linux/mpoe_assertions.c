#include "mpoe_types.h"

#include <linux/if.h>

/*
 * This file runs build-time assertions without ever being linked to anybody
 */

#define CHECK(x) do { char (*a)[(x) ? 1 : -1] = 0; (void) a; } while (0)

void
assertions(void)
{
  CHECK(MPOE_IF_NAMESIZE == IFNAMSIZ);
  CHECK(sizeof(struct mpoe_mac_addr) == sizeof(((struct ethhdr *)NULL)->h_dest));
  CHECK(sizeof(struct mpoe_mac_addr) == sizeof(((struct ethhdr *)NULL)->h_source));
}
