#define FP_COMPONENT "focaltech_c652"

#include "drivers_api.h"
#include <stdio.h>

#define FOCALTECH_C652_DRIVER_FULLNAME "FocalTech c652"

#define FOCALTECH_C652_CMD_TIMEOUT 1000

#define IMAGE_WIDTH 64
#define IMAGE_HEIGHT 80
#define IMAGE_DATA_LENGTH 10246

#define CMD_STATUS_CHECK      0x37
#define CMD_PREPARE_SENSOR    0x80
#define CMD_TRIGGER_CAPTURE   0x82
#define CMD_REQUEST_IMAGE     0x81
#define CMD_WAIT_FINGER       0x80

static const guint8 payload_status_check[] = { 0x01, 0x01, 0x01 };
static const guint8 payload_prepare_sensor[] = { 0x02, 0x01 };
static const guint8 payload_trigger_capture[] = { 0x73, 0x01 };
static const guint8 payload_wait_finger[] = { 0x02 };

G_DECLARE_FINAL_TYPE (FpiDeviceFocaltechC652, fpi_device_focaltech_c652, FPI, DEVICE_FOCALTECH_C652, FpImageDevice)

struct _FpiDeviceFocaltechC652
{
  FpImageDevice parent;

  guint8          bulk_in_ep;
  guint8          bulk_out_ep;

  FpiSsm *ssm;
};

G_DEFINE_TYPE (FpiDeviceFocaltechC652, fpi_device_focaltech_c652, FP_TYPE_IMAGE_DEVICE);

static const FpIdEntry id_table[] = {
  { .vid = 0x2808,  .pid = 0xc652,  },
  { .vid = 0,  .pid = 0,  .driver_data = 0 },
};

static GBytes *
focaltech_compose_cmd (const guint8 *payload, guint16 len)
{
  GByteArray *array = g_byte_array_new ();
  guint8 bcc = 0;
  guint8 len_8 = (guint8) len;

  g_byte_array_append (array, (const guint8 []) { 0x02, 0x00 }, 2);

  g_byte_array_append (array, &len_8, 1);

  g_byte_array_append (array, payload, len);

  bcc ^= len_8;
  for (int i = 0; i < len; i++)
    bcc ^= payload[i];
  g_byte_array_append (array, &bcc, 1);

  return g_byte_array_free_to_bytes (array);
}

static void
dev_open (FpImageDevice *dev)
{
  FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);

  self->bulk_in_ep = 0x82;
  self->bulk_out_ep = 0x01;

  fpi_image_device_open_complete (dev, NULL);
}

static void
dev_close (FpImageDevice *dev)
{
  fpi_image_device_close_complete (dev, NULL);
}

static void dev_activate (FpImageDevice *dev)
{
  fpi_image_device_activate_complete (dev, NULL);
}

static void dev_deactivate (FpImageDevice *dev)
{
  fpi_image_device_deactivate_complete (dev, NULL);
}

static void
capture_run_state (FpiSsm *ssm, FpDevice *dev);

enum {
  CAPTURE_START,
  CAPTURE_STATUS_CHECK,
  CAPTURE_PREPARE_SENSOR,
  CAPTURE_TRIGGER,
  CAPTURE_REQUEST_IMAGE,
  CAPTURE_PROCESS_IMAGE,
  CAPTURE_DONE,
  CAPTURE_NUM_STATES
};

static void
finger_wait_off_run_state (FpiSsm *ssm, FpDevice *dev);

enum {
    FINGER_WAIT_OFF_START,
    FINGER_WAIT_OFF_WAIT,
    FINGER_WAIT_OFF_NUM_STATES
};

static void
finger_wait_off_complete (FpiSsm *ssm, FpDevice *dev, GError *error)
{
    if (error)
    {
        fpi_image_device_session_error (FP_IMAGE_DEVICE (dev), error);
        return;
    }

    fpi_image_device_report_finger_status (FP_IMAGE_DEVICE (dev), FALSE);
}

static void
capture_ssm_complete (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  if (error)
    fpi_image_device_session_error (FP_IMAGE_DEVICE (dev), error);
  else
    {
        FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652(dev);
        self->ssm = fpi_ssm_new (FP_DEVICE (self), finger_wait_off_run_state, FINGER_WAIT_OFF_NUM_STATES);
        fpi_ssm_start (self->ssm, finger_wait_off_complete);
    }
}

static void
dev_capture (FpiDeviceFocaltechC652 *self)
{
  self->ssm = fpi_ssm_new (FP_DEVICE (self), capture_run_state, CAPTURE_NUM_STATES);
  fpi_ssm_start (self->ssm, capture_ssm_complete);
}

static void
finger_detect_run_state (FpiSsm *ssm, FpDevice *dev);

enum {
    FINGER_DETECT_START,
    FINGER_DETECT_WAIT,
    FINGER_DETECT_NUM_STATES
};

static void
finger_detect_complete (FpiSsm *ssm, FpDevice *dev, GError *error)
{
    if (error)
    {
        fpi_image_device_session_error (FP_IMAGE_DEVICE (dev), error);
        return;
    }

    fpi_image_device_report_finger_status (FP_IMAGE_DEVICE (dev), TRUE);
    dev_capture(FPI_DEVICE_FOCALTECH_C652(dev));
}

static void
dev_change_state (FpImageDevice *dev, FpiImageDeviceState state)
{
  if (state == FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON)
    {
        FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652(dev);
        self->ssm = fpi_ssm_new (FP_DEVICE (self), finger_detect_run_state, FINGER_DETECT_NUM_STATES);
        fpi_ssm_start (self->ssm, finger_detect_complete);
    }
}


// enrolling works fine, verifying does not
// looking at other drivers, they had issue with small sensor sizes too
// what they did was increase the image they're sending by 2 for example
// might wanna do that for testing?
static void
capture_process_image (FpiSsm *ssm, FpDevice *dev, GBytes *image_data)
{
  gsize data_len;
    const guint8 *data = g_bytes_get_data (image_data, &data_len);

    guint num_pixels = IMAGE_WIDTH * IMAGE_HEIGHT;
    g_autoptr(GByteArray) combined_pixels = g_byte_array_new_take (g_malloc0 (num_pixels), num_pixels);

    for (int i = 0; i < num_pixels; i++)
    {
        int offset = 4 + i * 2; // HEADER_OFFSET + i * PIXEL_STRIDE
        if (offset < data_len)
        {
            combined_pixels->data[i] = data[offset];
        }
        else
        {
            // data is incomplete
            combined_pixels->data[i] = 0;
        }
    }

    FpImage *image = fp_image_new (IMAGE_WIDTH, IMAGE_HEIGHT);
    memcpy(image->data, combined_pixels->data, num_pixels);

    fpi_image_device_image_captured (FP_IMAGE_DEVICE (dev), image);

    fpi_ssm_mark_completed (ssm);
}

static void
capture_cmd_read_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
  FpiSsm *ssm = transfer->ssm;
  if (error)
    {
      fpi_ssm_mark_failed (ssm, error);
      return;
    }

  if (fpi_ssm_get_cur_state (ssm) == CAPTURE_REQUEST_IMAGE)
    capture_process_image (ssm, dev, g_bytes_new (transfer->buffer, transfer->actual_length));
  else
    fpi_ssm_next_state (ssm);
}

static void
capture_cmd_write_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
    FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
    FpiSsm *ssm = transfer->ssm;
    gsize response_len = 7;

    if (error)
    {
        fpi_ssm_mark_failed (ssm, error);
        return;
    }

    if (fpi_ssm_get_cur_state (ssm) == CAPTURE_REQUEST_IMAGE)
        response_len = IMAGE_DATA_LENGTH;

    FpiUsbTransfer *in_transfer = fpi_usb_transfer_new (dev);
    in_transfer->ssm = ssm;
    fpi_usb_transfer_fill_bulk (in_transfer, self->bulk_in_ep, response_len);
    fpi_usb_transfer_submit (in_transfer, FOCALTECH_C652_CMD_TIMEOUT, NULL, capture_cmd_read_cb, NULL);
}

static void
capture_run_state (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
  g_autoptr(GBytes) command = NULL;
  g_autoptr(GByteArray) payload = g_byte_array_new();

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case CAPTURE_START:
        fpi_ssm_next_state (ssm);
        return;
    case CAPTURE_STATUS_CHECK:
      g_byte_array_append(payload, (const guint8 []){CMD_STATUS_CHECK}, 1);
      g_byte_array_append(payload, payload_status_check, sizeof(payload_status_check));
      break;
    case CAPTURE_PREPARE_SENSOR:
      g_byte_array_append(payload, (const guint8 []){CMD_PREPARE_SENSOR}, 1);
      g_byte_array_append(payload, payload_prepare_sensor, sizeof(payload_prepare_sensor));
      break;
    case CAPTURE_TRIGGER:
      g_byte_array_append(payload, (const guint8 []){CMD_TRIGGER_CAPTURE}, 1);
      g_byte_array_append(payload, payload_trigger_capture, sizeof(payload_trigger_capture));
      break;
    case CAPTURE_REQUEST_IMAGE:
      g_byte_array_append(payload, (const guint8 []){CMD_REQUEST_IMAGE}, 1);
      break;
    case CAPTURE_PROCESS_IMAGE:
    case CAPTURE_DONE:
      fpi_ssm_mark_completed (ssm);
      return;
    }
  command = focaltech_compose_cmd(payload->data, (guint8) payload->len);

  FpiUsbTransfer *out_transfer = fpi_usb_transfer_new (dev);
  out_transfer->ssm = ssm;
  fpi_usb_transfer_fill_bulk_full (out_transfer, self->bulk_out_ep, (guint8 *) g_bytes_get_data (command, NULL), g_bytes_get_size (command), NULL);
  fpi_usb_transfer_submit (out_transfer, FOCALTECH_C652_CMD_TIMEOUT, NULL, capture_cmd_write_cb, NULL);
}

static void
finger_detect_read_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error);
static void finger_wait_off_read_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error);

static void
finger_detect_write_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
    FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
    FpiSsm *ssm = transfer->ssm;

    if (error)
    {
        fpi_ssm_mark_failed (ssm, error);
        return;
    }

    FpiUsbTransfer *in_transfer = fpi_usb_transfer_new (dev);
    in_transfer->ssm = ssm;
    fpi_usb_transfer_fill_bulk (in_transfer, self->bulk_in_ep, 7);
    fpi_usb_transfer_submit (in_transfer, FOCALTECH_C652_CMD_TIMEOUT, NULL, finger_detect_read_cb, NULL);
}

static void
finger_wait_off_write_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
    FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
    FpiSsm *ssm = transfer->ssm;

    if (error)
    {
        fpi_ssm_mark_failed (ssm, error);
        return;
    }

    FpiUsbTransfer *in_transfer = fpi_usb_transfer_new (dev);
    in_transfer->ssm = ssm;
    fpi_usb_transfer_fill_bulk (in_transfer, self->bulk_in_ep, 7);
    fpi_usb_transfer_submit (in_transfer, FOCALTECH_C652_CMD_TIMEOUT, NULL, finger_wait_off_read_cb, NULL);
}

static void
finger_detect_run_state (FpiSsm *ssm, FpDevice *dev)
{
    FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
    g_autoptr(GBytes) command = NULL;
    g_autoptr(GByteArray) payload = g_byte_array_new();

    switch (fpi_ssm_get_cur_state (ssm))
    {
        case FINGER_DETECT_START:
            fpi_ssm_next_state_delayed (ssm, 50);
            return;
        case FINGER_DETECT_WAIT:
            g_byte_array_append(payload, (const guint8 []){CMD_WAIT_FINGER}, 1);
            g_byte_array_append(payload, payload_wait_finger, sizeof(payload_wait_finger));
            break;
    }
    command = focaltech_compose_cmd(payload->data, (guint8) payload->len);

    FpiUsbTransfer *out_transfer = fpi_usb_transfer_new (dev);
    out_transfer->ssm = ssm;
    fpi_usb_transfer_fill_bulk_full (out_transfer, self->bulk_out_ep, (guint8 *) g_bytes_get_data (command, NULL), g_bytes_get_size (command), NULL);
    fpi_usb_transfer_submit (out_transfer, FOCALTECH_C652_CMD_TIMEOUT, NULL, finger_detect_write_cb, NULL);
}

static void
finger_wait_off_run_state (FpiSsm *ssm, FpDevice *dev)
{
    FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
    g_autoptr(GBytes) command = NULL;
    g_autoptr(GByteArray) payload = g_byte_array_new();

    switch (fpi_ssm_get_cur_state (ssm))
    {
        case FINGER_WAIT_OFF_START:
            fpi_ssm_next_state_delayed (ssm, 50);
            return;
        case FINGER_WAIT_OFF_WAIT:
            g_byte_array_append(payload, (const guint8 []){CMD_WAIT_FINGER}, 1);
            g_byte_array_append(payload, payload_wait_finger, sizeof(payload_wait_finger));
            break;
    }
    command = focaltech_compose_cmd(payload->data, (guint8) payload->len);

    FpiUsbTransfer *out_transfer = fpi_usb_transfer_new (dev);
    out_transfer->ssm = ssm;
    fpi_usb_transfer_fill_bulk_full (out_transfer, self->bulk_out_ep, (guint8 *) g_bytes_get_data (command, NULL), g_bytes_get_size (command), NULL);
    fpi_usb_transfer_submit (out_transfer, FOCALTECH_C652_CMD_TIMEOUT, NULL, finger_wait_off_write_cb, NULL);
}

static void
finger_detect_read_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
    FpiSsm *ssm = transfer->ssm;
    if (error)
    {
        fpi_ssm_mark_failed (ssm, error);
        return;
    }

    if (transfer->actual_length > 4 && transfer->buffer[4] == 1)
        fpi_ssm_mark_completed (ssm);
    else
        fpi_ssm_jump_to_state_delayed (ssm, FINGER_DETECT_WAIT, 50);
}

static void
finger_wait_off_read_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
    FpiSsm *ssm = transfer->ssm;
    if (error)
    {
        fpi_ssm_mark_failed (ssm, error);
        return;
    }

    if (transfer->actual_length > 4 && transfer->buffer[4] != 1)
        fpi_ssm_mark_completed (ssm);
    else
        fpi_ssm_jump_to_state_delayed (ssm, FINGER_WAIT_OFF_WAIT, 50);
}

static void
fpi_device_focaltech_c652_init (FpiDeviceFocaltechC652 *self)
{
}

static void
fpi_device_focaltech_c652_class_init (FpiDeviceFocaltechC652Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = FOCALTECH_C652_DRIVER_FULLNAME;

  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->id_table = id_table;

  img_class->img_open = dev_open;
  img_class->img_close = dev_close;
  img_class->activate = dev_activate;
  img_class->deactivate = dev_deactivate;
  img_class->change_state = dev_change_state;

  img_class->bz3_threshold = 9;
}
