/* KallistiOS ##version##

   dc/maple/dreameye.h
   Copyright (C) 2005, 2009, 2010 Lawrence Sebald
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/maple/dreameye.h
    \brief   Definitions for using the Dreameye Camera device.
    \ingroup peripherals_camera

    This file contains the definitions needed to access the Maple Camera type
    device (aka, the Dreameye). Currently, this driver allows you to download
    the still pictures that are saved on the camera and delete them. It does not
    allow you to use the camera for video input currently.

    \author Lawrence Sebald
    \author Joseph Black
*/

#ifndef __DC_MAPLE_DREAMEYE_H
#define __DC_MAPLE_DREAMEYE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <stddef.h>
#include <dc/maple.h>

/** \defgroup peripherals_camera    Camera
    \brief                          Maple driver for the DreamEye peripheral
    \ingroup                        peripherals

    @{
*/

/** \brief  Dreameye status structure.

    This structure contains information about the status of the Camera device
    and can be fetched with maple_dev_status(). You should not change any of
    this information, it should all be considered read-only. Most of the fields
    in here are related to image transfers, and messing with them during a
    transfer could screw things up.

    \headerfile dc/maple/dreameye.h
*/
typedef struct dreameye_state {
    /** \brief  The number of images on the device. */
    int             image_count;

    /** \brief  Is the image_count field valid? */
    int             image_count_valid;

    /** \brief  The number of transfer operations required for the selected
                image. */
    int             transfer_count;

    /** \brief  Is an image transferring now? */
    int             img_transferring;

    /** \brief  Storage for image data. */
    uint8_t        *img_buf;

    /** \brief  The size of the image in bytes. */
    int             img_size;

    /** \brief  The image number currently being transferred. */
    uint8_t         img_number;
} dreameye_state_t;

/** \brief Camera operation type. */
typedef enum dreameye_operation {
    DREAMEYE_OPERATION_NONE = 0,       /**< No operation pending. */
    DREAMEYE_OPERATION_IMAGE_COUNT,    /**< Querying stored-image count. */
    DREAMEYE_OPERATION_TRANSFER_COUNT, /**< Querying image transfer count. */
    DREAMEYE_OPERATION_IMAGE_READ,     /**< Reading a stored image. */
    DREAMEYE_OPERATION_ERASE           /**< Erasing stored image data. */
} dreameye_operation_t;

/** \brief Stored-image transfer state. */
typedef enum dreameye_transfer_state {
    DREAMEYE_TRANSFER_IDLE = 0,        /**< No transfer has run. */
    DREAMEYE_TRANSFER_QUERYING,        /**< Reading transfer geometry. */
    DREAMEYE_TRANSFER_RECEIVING,       /**< Receiving image chunks. */
    DREAMEYE_TRANSFER_COMPLETE,        /**< Last transfer completed. */
    DREAMEYE_TRANSFER_ERROR,           /**< Last transfer failed. */
    DREAMEYE_TRANSFER_DISCONNECTED     /**< Device detached during transfer. */
} dreameye_transfer_state_t;

/** \brief Coherent camera status snapshot. */
typedef struct dreameye_status {
    dreameye_operation_t pending_operation; /**< Operation currently pending. */
    dreameye_operation_t last_operation;    /**< Most recently completed operation. */
    dreameye_transfer_state_t transfer_state; /**< Stored-image transfer state. */
    int last_result;               /**< Most recent MAPLE_E* result. */
    int last_response;             /**< Most recent raw Maple response. */
    uint32_t device_error;         /**< Last camera error payload, if any. */
    uint32_t sequence;             /**< Incremented for every published result. */
    int image_count;               /**< Number of stored images. */
    int image_count_valid;         /**< Whether image_count is current. */
    uint16_t transfer_count;       /**< Expected chunks for the current image. */
    uint16_t chunks_received;      /**< Unique chunks received. */
    size_t bytes_received;         /**< Logical image bytes received. */
    uint32_t malformed_responses;  /**< Rejected malformed response count. */
    uint32_t command_failures;     /**< Device or transport failure count. */
    uint32_t command_timeouts;     /**< Caller-visible deadline count. */
} dreameye_status_t;

/** \brief Default deadline for a complete stored-image transfer. */
#define DREAMEYE_DEFAULT_TRANSFER_TIMEOUT 5000u

/** \brief  Get the number of images on the device.

    This constant is used with the MAPLE_COMMAND_GETCOND command to fetch the
    number of images on the device.
*/
#define DREAMEYE_GETCOND_NUM_IMAGES     0x81

/** \brief  Get the number of transfers to copy an image.

     This constant is used with the MAPLE_COMMAND_GETCOND command to fetch the
     number of times a transfer command must be sent to get the image specified.
*/
#define DREAMEYE_GETCOND_TRANSFER_COUNT 0x83

/** \brief  Get an image from the device.

    This subcommand is used with the MAPLE_COMMAND_CAMCONTROL command to fetch
    part of image data from the specified image.
*/
#define DREAMEYE_SUBCOMMAND_IMAGEREQ    0x04

/** \brief  Erase an image from the device.

    This subcommand is used with the MAPLE_COMMAND_CAMCONTROL command to remove
    an image from the device.
*/
#define DREAMEYE_SUBCOMMAND_ERASE       0x05

/** \brief  Error return command.

     This subcommand is used by the dreameye with the MAPLE_COMMAND_CAMCONTROL
     command to indicate an error occurred in a subcommand.
*/
#define DREAMEYE_SUBCOMMAND_ERROR       0xFF

/** \brief  Continue transferring an image. */
#define DREAMEYE_IMAGEREQ_CONTINUE      0x00

/** \brief  Start transferring an image from its start. */
#define DREAMEYE_IMAGEREQ_START         0x40

/** \brief  Get the number of images on the Dreameye.

    This function fetches the number of saved images on the specified Dreameye
    device. It can be sent to any of the subdevices of the MAPLE_FUNC_CONTROLLER
    root device of the Dreameye. When the response comes from the device, the
    image_count field of the dreameye_state_t for the specified device will have
    the number of images on the device, and image_count_valid will be set to 1.

    \param  dev             The device to query.
    \param  block           Set to 1 to wait for the Dreameye to respond.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_ETIMEOUT  The command timed out while blocking.
    \retval MAPLE_EAGAIN    Could not send the command to the device, try again.
*/
int dreameye_get_image_count(maple_device_t *dev, int block);

/** \brief  Transfer an image from the Dreameye.

    This function fetches a single image from the specified Dreameye device.
    This function will block, and can take a little while to execute. You must
    use the first subdevice of the MAPLE_FUNC_CONTROLLER root device of the
    Dreameye as the dev parameter.

    \param  dev             The device to get an image from.
    \param  image           The image number to download.
    \param  data            A pointer to a buffer to store things in. This
                            will be allocated by the function and you are
                            responsible for freeing the data when you are done.
    \param  img_sz          A pointer to storage for the size of the image, in
                            bytes.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EFAIL     On error.
*/
int dreameye_get_image(maple_device_t *dev, uint8_t image, uint8_t **data,
                       int *img_sz);

/** \brief Transfer an image with an explicit deadline.

    This is the bounded form of dreameye_get_image(). Output values are cleared
    before submission. A timeout stops issuing new chunks; already submitted
    Maple frames retain their ownership until their completion path consumes
    them, so their DMA buffers are never reused unsafely.

    \param dev             Unit 1 of the camera device.
    \param image           Stored image number, from 0x02 through 0x21.
    \param data            Receives an allocated image buffer on success.
    \param img_sz          Receives the image size in bytes on success.
    \param timeout_ms      Nonzero overall deadline in milliseconds.

    \retval MAPLE_EOK      Image transferred successfully.
    \retval MAPLE_EINVALID Invalid argument or device topology.
    \retval MAPLE_EAGAIN   The port or a required frame is busy.
    \retval MAPLE_ETIMEOUT The transfer deadline expired.
    \retval MAPLE_EFAIL    Device, allocation, or protocol failure.
*/
int dreameye_get_image_timed(maple_device_t *dev, uint8_t image,
                             uint8_t **data, int *img_sz,
                             uint32_t timeout_ms);

/** \brief Query the number of chunks required by a stored image.

    \param dev             Camera subdevice to query.
    \param image           Stored image number, from 0x02 through 0x21.
    \param transfer_count  Receives a value from 1 through 256.

    \retval MAPLE_EOK      Query completed successfully.
    \retval MAPLE_EINVALID Invalid argument, device, or returned geometry.
    \retval MAPLE_EAGAIN   Device frame or port is busy.
    \retval MAPLE_ETIMEOUT Device did not complete before the command deadline.
    \retval MAPLE_EFAIL    Device or protocol failure.
*/
int dreameye_get_image_transfer_count(maple_device_t *dev, uint8_t image,
                                      uint16_t *transfer_count);

/** \brief Retrieve a coherent camera status snapshot.

    \retval 0              On success.
    \retval -1             On error with errno set.
*/
int dreameye_get_status(const maple_device_t *dev,
                        dreameye_status_t *status);

/** \brief  Erase an image from the Dreameye.

    This function erases the specified image from the Dreameye device. This
    command can be sent to any of the subdevices of the MAPLE_FUNC_CONTROLLER
    root device of the Dreameye.

    \param  dev             The device to erase from.
    \param  image           The image number to erase (0xFF to erase all).
    \param  block           Set to 1 to wait for the Dreameye to respond.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EAGAIN    Couldn't send the command, try again.
    \retval MAPLE_ETIMEOUT  Timeout on blocking.
    \retval MAPLE_EINVALID  Invalid image number specified.
*/
int dreameye_erase_image(maple_device_t *dev, uint8_t image, int block);

/* \cond */
/* Init / Shutdown */
void dreameye_init(void);
void dreameye_shutdown(void);
/* \endcond */

/** @} */

__END_DECLS

#endif  /* __DC_MAPLE_DREAMEYE_H */
