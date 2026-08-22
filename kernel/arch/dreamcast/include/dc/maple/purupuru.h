/* KallistiOS ##version##

   dc/maple/purupuru.h
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2005, 2010 Lawrence Sebald
   Copyright (C) 2025 Donald Haase
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/maple/purupuru.h
    \brief   Definitions for using the Puru Puru (Jump) Pack.
    \ingroup peripherals_rumble

    This file contains the definitions needed to access Maple vibration-pack
    devices. The historical Puru Puru name is retained by the established KOS
    API.

    This driver is largely based off of information provided by Kamjin on the
    DCEmulation forums. See
    http://dcemulation.org/phpBB/viewtopic.php?f=29&t=48462 if you're interested
    in the original documentation.

    Not all vibration packs interpret effects identically. Some settings do not
    behave as expected on many devices; for example, decay can suppress an
    effect entirely on some first-party units. Applications should validate
    their chosen effects on the devices they intend to support.

    \author Lawrence Sebald
    \author Donald Haase
*/

#ifndef __DC_MAPLE_PURUPURU_H
#define __DC_MAPLE_PURUPURU_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <dc/maple.h>

/** \defgroup peripherals_rumble    Rumble Pack
    \brief                          Maple driver for vibration pack peripherals
    \ingroup                        peripherals

    @{
*/

/** \brief  Effect generation structure.

    This structure is used for convenience to send an effect to the jump pack.
    The members in the structure note general explanations of their use as well
    as some limitations and suggestions. There shouldn't be a need to use the
    raw accessor with the new fully specified members.
*/
typedef union purupuru_effect  {
    /** \brief Access the raw 32-bit value to be sent to the puru */
    uint32_t raw;
    /* \cond */
    /* Deprecated old structure which has been inverted now to union with raw. */
    struct {
        uint8_t special     __depr("Please see purupuru_effect_t which has new members.");
        uint8_t effect1     __depr("Please see purupuru_effect_t which has new members.");
        uint8_t effect2     __depr("Please see purupuru_effect_t which has new members.");
        uint8_t duration    __depr("Please see purupuru_effect_t which has new members.");
    };
    /* \endcond */
    struct {
        /** \brief Continuous Vibration. When set vibration will continue until stopped */
        bool     cont    : 1;
        /** \brief Reserved. Always 0s */
        uint32_t res     : 3;
        /** \brief Motor number. 0 will cause an error. 1 is the typical setting. */
        uint32_t motor   : 4;

        /** \brief Backward direction (- direction) intensity setting bits. 0 stops vibration. */
        uint32_t bpow    : 3;
        /** \brief Divergent vibration. The rumble will get stronger until it stops. */
        bool     div     : 1;
        /** \brief Forward direction (+ direction) intensity setting bits. 0 stops vibration. */
        uint32_t fpow    : 3;
        /** \brief Convergent vibration. The rumble will get weaker until it stops. */
        bool     conv    : 1;

        /** \brief Vibration frequency. for most purupuru 4-59. */
        uint8_t  freq;
        /** \brief Vibration inclination period. */
        uint8_t  inc;
    };
} purupuru_effect_t;

_Static_assert(sizeof(purupuru_effect_t) == 4, "Invalid effect size");

/** \brief Maximum number of vibration units described by the protocol. */
#define PURUPURU_MAX_UNITS 15

/** \brief Direction of an effect's power ramp. */
typedef enum purupuru_ramp {
    PURUPURU_RAMP_NONE = 0,     /**< \brief Hold the requested power. */
    PURUPURU_RAMP_UP,           /**< \brief Increase positive-direction power. */
    PURUPURU_RAMP_DOWN          /**< \brief Decrease negative-direction power. */
} purupuru_ramp_t;

/** \brief Typed description of one vibration effect. */
typedef struct purupuru_effect_config {
    uint8_t unit;               /**< \brief Vibration unit number, 1 through 15. */
    int8_t power;               /**< \brief Signed power from -7 through 7. */
    uint8_t frequency;          /**< \brief Device-defined pulsation frequency. */
    uint8_t cycles;             /**< \brief Cycles per ramp step, encoded minus one. */
    bool continuous;            /**< \brief Continue until stopped or auto-stopped. */
    purupuru_ramp_t ramp;       /**< \brief Optional power-ramp direction. */
} purupuru_effect_config_t;

/** \brief Summary advertised by a vibration device. */
typedef struct purupuru_info {
    uint8_t units;              /**< \brief Number of vibration units. */
    uint8_t simultaneous_units; /**< \brief Units accepted by one command. */
} purupuru_info_t;

/** \brief Physical position of a vibration unit. */
typedef enum purupuru_unit_position {
    PURUPURU_POSITION_FRONT = 0,
    PURUPURU_POSITION_BACK,
    PURUPURU_POSITION_LEFT,
    PURUPURU_POSITION_RIGHT
} purupuru_unit_position_t;

/** \brief Axis along which a vibration unit acts. */
typedef enum purupuru_unit_axis {
    PURUPURU_AXIS_NONE = 0,
    PURUPURU_AXIS_X,
    PURUPURU_AXIS_Y,
    PURUPURU_AXIS_Z
} purupuru_unit_axis_t;

/** \brief Frequency encoding used by a vibration unit. */
typedef enum purupuru_frequency_mode {
    PURUPURU_FREQUENCY_RANGE = 0,   /**< \brief Inclusive minimum and maximum. */
    PURUPURU_FREQUENCY_FIXED = 1,   /**< \brief One supported frequency. */
    PURUPURU_FREQUENCY_NONE = 15,   /**< \brief Frequency is not configurable. */
    PURUPURU_FREQUENCY_UNKNOWN = 255
} purupuru_frequency_mode_t;

/** \brief Capabilities of one vibration unit. */
typedef struct purupuru_unit_info {
    uint8_t unit;                       /**< \brief Unit number. */
    purupuru_unit_position_t position;  /**< \brief Unit position. */
    purupuru_unit_axis_t axis;          /**< \brief Vibration axis. */
    bool variable_power;                /**< \brief Power is configurable. */
    bool continuous;                    /**< \brief Continuous mode is supported. */
    bool directional;                   /**< \brief Signed direction is supported. */
    bool arbitrary_waveform;            /**< \brief Device advertises waveform data. */
    purupuru_frequency_mode_t frequency_mode; /**< \brief Frequency encoding. */
    uint8_t minimum_frequency;                /**< \brief Inclusive minimum. */
    uint8_t maximum_frequency;                /**< \brief Inclusive maximum. */
} purupuru_unit_info_t;

/** \brief Orientation of a vibration device relative to its parent. */
typedef enum purupuru_direction {
    PURUPURU_DIRECTION_NORMAL = 0,
    PURUPURU_DIRECTION_FLIPPED,
    PURUPURU_DIRECTION_LEFT,
    PURUPURU_DIRECTION_RIGHT
} purupuru_direction_t;

/** \brief Coherent completion state for vibration output commands. */
typedef struct purupuru_status {
    bool busy;                  /**< \brief A command is queued or on the bus. */
    int result;                /**< \brief MAPLE_EOK, MAPLE_EFAIL, or MAPLE_EAGAIN. */
    int response;              /**< \brief Raw MAPLE_RESPONSE_* value. */
    uint8_t command;           /**< \brief Last submitted MAPLE_COMMAND_* value. */
    uint8_t effect_count;      /**< \brief Effect words in the last command. */
    uint32_t submitted_sequence; /**< \brief Most recent submission. */
    uint32_t completed_sequence; /**< \brief Most recent completion. */
} purupuru_status_t;

/** \brief IRQ-context vibration command completion callback. */
typedef void (*purupuru_completion_handler_t)(maple_device_t *dev,
                                              int result,
                                              int response,
                                              uint32_t sequence,
                                              void *user_data);

/* Compat */
#define PURUPURU_DEPRECATED \
    __depr("Please see purupuru_effect_t for modern equivalent.")

static inline uint32_t PURUPURU_DEPRECATED
PURUPURU_EFFECT2_UINTENSITY(uint8_t x) {
    return x << 4;
}

static inline uint32_t PURUPURU_DEPRECATED
PURUPURU_EFFECT2_LINTENSITY(uint8_t x) {
    return x;
}

static inline uint32_t PURUPURU_DEPRECATED
PURUPURU_EFFECT1_INTENSITY(uint8_t x) {
    return x << 4;
}

static const uint8_t PURUPURU_EFFECT2_DECAY PURUPURU_DEPRECATED = 8 << 4;
static const uint8_t PURUPURU_EFFECT2_PULSE PURUPURU_DEPRECATED = 8;
static const uint8_t PURUPURU_EFFECT1_PULSE PURUPURU_DEPRECATED = 8 << 4;
static const uint8_t PURUPURU_EFFECT1_POWERSAVE PURUPURU_DEPRECATED = 15;
static const uint8_t PURUPURU_SPECIAL_MOTOR1 PURUPURU_DEPRECATED = 1 << 4;
static const uint8_t PURUPURU_SPECIAL_MOTOR2 PURUPURU_DEPRECATED = 1 << 7;
static const uint8_t PURUPURU_SPECIAL_PULSE PURUPURU_DEPRECATED = 1;

#undef PURUPURU_DEPRECATED

/** \brief Encode a typed effect without relying on C bitfield layout.

    A zero-power effect is the portable stop form. Ramping up requires positive
    power, ramping down requires negative power, and a non-ramping effect must
    use zero cycles.

    \param  config          Typed effect description.
    \param  effect          Receives the four-byte Maple effect word.
    \retval 0               Effect encoded successfully.
    \retval -1              Invalid pointer or field; errno is EINVAL.
*/
int purupuru_effect_encode(const purupuru_effect_config_t *config,
                           purupuru_effect_t *effect);

/** \brief Decode an effect word into an unambiguous typed description.

    \param  effect          Effect word to decode.
    \param  config          Receives the typed description.
    \retval 0               Effect decoded successfully.
    \retval -1              Invalid or ambiguous effect; errno is EINVAL or
                            EPROTO.
*/
int purupuru_effect_decode(const purupuru_effect_t *effect,
                           purupuru_effect_config_t *config);

/** \brief Return the unit counts advertised by a vibration device.

    \param  dev             Vibration device to inspect.
    \param  info            Receives device information.
    \retval 0               Information returned.
    \retval -1              Invalid argument or unavailable device; errno is
                            EINVAL, ENODEV, or EPROTO.
*/
int purupuru_get_info(const maple_device_t *dev, purupuru_info_t *info);

/** \brief Query one unit's capabilities from the device.

    This function performs one bounded Maple GETMINFO transaction and may block
    for up to 100 milliseconds. A timed-out frame remains owned by the driver
    until its eventual bus completion, so later submissions can temporarily
    report MAPLE_EAGAIN without risking reuse of an in-flight DMA frame.

    \param  dev             Vibration device to query.
    \param  unit            Unit number, starting at one.
    \param  info            Receives unit information.
    \retval 0               Information returned.
    \retval -1              Query failed; errno describes the reason.
*/
int purupuru_get_unit_info(maple_device_t *dev, uint8_t unit,
                           purupuru_unit_info_t *info);

/** \brief Determine device orientation relative to its parent peripheral.

    \param  dev             Attached vibration device.
    \param  direction       Receives the relative orientation.
    \retval 0               Direction returned.
    \retval -1              Invalid topology or unavailable device; errno is
                            EINVAL, ENODEV, or EPROTO.
*/
int purupuru_get_direction(const maple_device_t *dev,
                           purupuru_direction_t *direction);

/** \brief Check whether the device can accept another command.

    \retval 1               Device frame is available.
    \retval 0               A command still owns the device frame.
    \retval -1              Invalid or disconnected device; errno is set.
*/
int purupuru_is_ready(const maple_device_t *dev);

/** \brief Copy the device's coherent output-completion state.

    Sequence zero means that no output command has been submitted. A command's
    submission and completion sequence are equal after its response is
    processed.

    \param  dev             Vibration device to inspect.
    \param  status          Receives a coherent status snapshot.
    \retval 0               Status returned.
    \retval -1              Invalid or unavailable device; errno is set.
*/
int purupuru_get_status(const maple_device_t *dev,
                        purupuru_status_t *status);

/** \brief Set an optional completion callback for output commands.

    The handler runs in Maple interrupt context after coherent status is
    published and the device frame is unlocked. It must not block, allocate,
    perform filesystem I/O, or wait for another interrupt. Passing NULL removes
    the handler.

    \param  dev             Vibration device to configure.
    \param  handler         New callback, or NULL.
    \param  user_data       Opaque callback argument.
    \retval 0               Handler updated.
    \retval -1              Invalid or unavailable device; errno is set.
*/
int purupuru_set_completion_handler(maple_device_t *dev,
                                    purupuru_completion_handler_t handler,
                                    void *user_data);

/** \brief Submit effects for one or more vibration units atomically.

    The count may not exceed either the device's simultaneous-unit count or
    PURUPURU_MAX_UNITS. Unit numbers must be unique and advertised by the
    device. Completion is asynchronous; use purupuru_get_status() or install a
    completion handler.

    \param  dev             Destination vibration device.
    \param  effects         Array of effects.
    \param  count           Number of effects.
    \retval MAPLE_EOK       Command queued.
    \retval MAPLE_EAGAIN    Device frame is busy.
    \retval MAPLE_EINVALID  Invalid device, effect, or count.
    \retval MAPLE_EFAIL     Command could not be queued.
*/
int purupuru_rumble_many(maple_device_t *dev,
                         const purupuru_effect_t *effects, size_t count);

/** \brief Stop one vibration unit using the standard zero-power effect. */
int purupuru_stop(maple_device_t *dev, uint8_t unit);

/** \brief Set hardware auto-stop times for one or more units.

    Each time value is encoded in quarter-second units, with zero representing
    approximately 0.25 seconds and 255 approximately 64 seconds. Unit numbers
    must be unique and advertised by the device. This is an asynchronous output
    command with the same status and callback rules as purupuru_rumble_many().

    \param  dev             Destination vibration device.
    \param  units           Unit-number array.
    \param  times           Auto-stop time array.
    \param  count           Number of unit/time pairs.
*/
int purupuru_set_autostop_times(maple_device_t *dev, const uint8_t *units,
                                const uint8_t *times, size_t count);

/** \brief Set one unit's hardware auto-stop time. */
int purupuru_set_autostop_time(maple_device_t *dev, uint8_t unit,
                               uint8_t time);

/** \brief  Send an effect to a jump pack.

    This function sends an effect created with the purupuru_effect_t structure
    to a jump pack to be executed.

    \param  dev             The device to send the command to.
    \param  effect          The effect to send.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EAGAIN    If the command couldn't be sent. Try again later.
    \retval MAPLE_EINVALID  The command is not being sent due to invalid input.
    \retval MAPLE_EFAIL     The command could not be queued.
*/
int purupuru_rumble(maple_device_t *dev, const purupuru_effect_t *effect);

/** \brief  Send a raw effect to a jump pack.

    This function sends an effect to a jump pack to be executed. This is for if
    you want to bypass KOS-based error checking. This is not recommended except
    for testing purposes.

    \param  dev             The device to send the command to.
    \param  effect          The effect to send.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EAGAIN    If the command couldn't be sent. Try again later.
    \retval MAPLE_EINVALID  Invalid or disconnected device.
    \retval MAPLE_EFAIL     The command could not be queued.
*/
int purupuru_rumble_raw(maple_device_t *dev, uint32_t effect);

/* \cond */
/* Init / Shutdown */
void purupuru_init(void);
void purupuru_shutdown(void);
/* \endcond */

/** @} */

__END_DECLS

#endif  /* __DC_MAPLE_PURUPURU_H */
