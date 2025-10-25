import usb.core
import usb.util
import time
import os
import struct
from functools import reduce

IMAGE_WIDTH = 64
IMAGE_HEIGHT = 80
HEADER_OFFSET = 4
PIXEL_STRIDE = 2 
# Sensor seems to return 2 bytes per pixel - 10246 bytes
# 64*80 = 5120 * 2 = 10240 + 6 header

VENDOR_ID = 0x2808
PRODUCT_ID = 0xc652

OUT_ENDPOINT = 0x01
IN_ENDPOINT = 0x82

RESPONSE_ACK_LENGTH = 7
IMAGE_DATA_LENGTH = 10246

PAYLOAD_STATUS_CHECK = bytes.fromhex("37010101")
PAYLOAD_PREPARE_SENSOR = bytes.fromhex("800201")
PAYLOAD_TRIGGER_CAPTURE = bytes.fromhex("827301")
PAYLOAD_REQUEST_IMAGE = bytes.fromhex("81")

class FocaltechDevice:
    def __init__(self, vendor_id=VENDOR_ID, product_id=PRODUCT_ID):
        self.vendor_id = vendor_id
        self.product_id = product_id
        self.dev = None

    def open(self):
        self.dev = usb.core.find(idVendor=self.vendor_id, idProduct=self.product_id)
        if self.dev is None:
            raise ValueError(f"Device not found: Vendor ID {hex(self.vendor_id)}, Product ID {hex(self.product_id)}")
        
        print(f"Focaltech device found: {self.dev.product}")

        self.dev.set_configuration()
        
        try:
            self.dev.reset()
        except usb.core.USBError as e:
            print(f"Error resetting device: {e}")

        self._detach_kernel_driver()
        usb.util.claim_interface(self.dev, 0)
        print("Interface 0 claimed.")

    def close(self):
        if self.dev:
            usb.util.release_interface(self.dev, 0)
            usb.util.dispose_resources(self.dev)
            print("Interface 0 released and resources disposed.")
            self.dev = None

    def _detach_kernel_driver(self):
        for cfg in self.dev:
            for intf in cfg:
                if self.dev.is_kernel_driver_active(intf.bInterfaceNumber):
                    try:
                        self.dev.detach_kernel_driver(intf.bInterfaceNumber)
                    except usb.core.USBError as e:
                        print(f"Could not detach kernel driver from interface {intf.bInterfaceNumber}: {e}")

    def _build_command(self, payload):
        length = len(payload)
        checksum = reduce(lambda x, y: x ^ y, [length] + list(payload))
        command = bytes([0x02, 0x00, length]) + payload + bytes([checksum])
        return command

    def send_command(self, payload):
        command = self._build_command(payload)
        try:
            self.dev.write(OUT_ENDPOINT, command)
        except usb.core.USBError as e:
            print(f"Error sending command: {e}")
            raise

    def read_response(self, expected_length, timeout=1000):
        try:
            response = self.dev.read(IN_ENDPOINT, expected_length, timeout=timeout)
            return bytes(response)
        except usb.core.USBError as e:
            if e.errno != 110:
                print(f"Error reading response: {e}")
            return None

    def capture_image_data(self):
        self.send_command(PAYLOAD_STATUS_CHECK)
        self.read_response(RESPONSE_ACK_LENGTH)
        self.send_command(PAYLOAD_PREPARE_SENSOR)
        self.read_response(RESPONSE_ACK_LENGTH)
        self.send_command(PAYLOAD_TRIGGER_CAPTURE)
        self.read_response(RESPONSE_ACK_LENGTH)
        self.send_command(PAYLOAD_REQUEST_IMAGE)
        image_data = self.read_response(IMAGE_DATA_LENGTH)
        return image_data

def save_pgm_from_bytes(pixel_bytes, width, height, filename, max_val=255):
    try:
        pgm_header = f"P5\n{width} {height}\n{max_val}\n"
        with open(filename, "wb") as f:
            f.write(pgm_header.encode('ascii'))
            f.write(pixel_bytes)
        print(f"Image saved as {filename}")
    except Exception as e:
        print(f"Error saving PGM {filename}: {e}")

def main():
    device = FocaltechDevice()
    try:
        device.open()

        while True:
            print("Capturing image...")
            image_data = device.capture_image_data()
            if not image_data:
                time.sleep(0.1)
                continue
            
            width = IMAGE_WIDTH
            height = IMAGE_HEIGHT
            header_offset = HEADER_OFFSET
            pixel_stride = PIXEL_STRIDE
            num_pixels = width * height
            
            pixels_16bit = bytearray(num_pixels * 2)

            for i in range(num_pixels):
                offset = header_offset + i * pixel_stride
                if offset + 1 < len(image_data):
                    pixels_16bit[i*2] = image_data[offset]
                    pixels_16bit[i*2 + 1] = image_data[offset + 1]
            
            save_pgm_from_bytes(pixels_16bit, IMAGE_WIDTH, IMAGE_HEIGHT, "live_view_16bit.pgm", max_val=65535)
            print("Image updated.")
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nStopping...")
    except (ValueError, usb.core.USBError) as e:
        print(f"An error occurred: {e}")
    finally:
        device.close()

if __name__ == "__main__":
    main()
