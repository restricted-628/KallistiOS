/* KallistiOS ##version##

   maple_enum.c
   (c)2002 Megan Potter
   (c)2008 Lawrence Sebald
   Copyright (C) 2026 Joseph Black
 */

#include <dc/maple.h>

/* Return the number of connected devices */
int maple_enum_count(void) {
    size_t cnt = 0;

    for(size_t p = 0; p < MAPLE_PORT_COUNT; p++)
        for(size_t u = 0; u < MAPLE_UNIT_COUNT; u++) {
            if(maple_dev_valid(p,u))
                cnt++;
        }

    return cnt;
}

/* Return a raw device info struct for the given device */
maple_device_t *maple_enum_dev(int p, int u) {
    if(p < 0 || p >= MAPLE_PORT_COUNT || u < 0 || u >= MAPLE_UNIT_COUNT)
        return NULL;

    maple_device_t *rv = maple_state.ports[p].units[u];
    if(rv && rv->valid)
        return rv;
    return NULL;
}

/* Return one function's descriptor in the stored word order used by public
   capability masks. Devices only provide descriptors for their first three
   functions, ordered from the most-significant function bit downward. */
bool maple_dev_function_data(const maple_device_t *dev, uint32_t function,
                             uint32_t *data) {
    uint32_t higher_functions;
    unsigned int index;

    if(!dev || !data || !function || (function & (function - 1)) ||
       !(dev->info.functions & function))
        return false;

    higher_functions = ~(function | (function - 1));
    index = __builtin_popcount(dev->info.functions & higher_functions);

    if(index >= 3)
        return false;

    /* CONT_CAPABILITY_* and the other public descriptor masks were defined
       against this stored word order long before this helper existed. */
    *data = dev->info.function_data[index];
    return true;
}

/* Decode the two forms of the connection-direction byte. Root devices carry
   two packed two-bit socket directions. Attached devices carry a one-hot
   direction mask (zero is also the protocol's legacy encoding for top). */
bool maple_dev_connection_direction(const maple_device_t *dev,
                                    unsigned int connection,
                                    maple_connection_direction_t *direction) {
    uint8_t raw;

    if(!dev || !direction || connection >= 2)
        return false;

    raw = dev->info.connector_direction;

    if(dev->unit == 0) {
        *direction = (maple_connection_direction_t)
                     ((raw >> (connection * 2)) & 0x03);
        return true;
    }

    if(connection != 0)
        return false;

    switch(raw & 0x0f) {
        case 0x00:
        case 0x01:
            *direction = MAPLE_CONNECTION_TOP;
            return true;
        case 0x02:
            *direction = MAPLE_CONNECTION_BOTTOM;
            return true;
        case 0x04:
            *direction = MAPLE_CONNECTION_LEFT;
            return true;
        case 0x08:
            *direction = MAPLE_CONNECTION_RIGHT;
            return true;
        default:
            return false;
    }
}

/* Return the Nth device of the requested type (where N is zero-indexed) */
maple_device_t *maple_enum_type(int n, uint32_t func) {

    if(n < 0)
        return NULL;

    for(size_t p = 0; p < MAPLE_PORT_COUNT; p++) {
        for(size_t u = 0; u < MAPLE_UNIT_COUNT; u++) {
            maple_device_t *dev = maple_enum_dev(p, u);

            if(dev != NULL && (dev->info.functions & func)) {
                if(!n) return dev;

                n--;
            }
        }
    }

    return NULL;
}

/* Return the Nth device that is of the requested type and supports the list of
   capabilities given. */
maple_device_t *maple_enum_type_ex(int n, uint32_t func, uint32_t cap) {
    uint32_t function_data;

    /* Function-data indexing is only defined for one function at a time. */
    if(n < 0 || !func || (func & (func - 1)))
        return NULL;

    for(size_t p = 0; p < MAPLE_PORT_COUNT; ++p) {
        for(size_t u = 0; u < MAPLE_UNIT_COUNT; ++u) {
            maple_device_t *dev = maple_enum_dev(p, u);

            if(dev != NULL && maple_dev_function_data(dev, func,
                                                       &function_data)) {
                if((function_data & cap) == cap) {
                    if(!n)
                        return dev;

                    --n;
                }
            }
        }
    }

    return NULL;
}

/* Get the status struct for the requested maple device.
   Cast to the appropriate type you're expecting. */
void *maple_dev_status(maple_device_t *dev) {
    /* The device must be valid. */
    if(!dev || !dev->valid || !dev->drv)
        return NULL;

    /* Cast and return the status buffer */
    return dev->status;
}
