# libfprint-focaltech-0xc652

This project contains a test driver for libfprint with FocalTech's fingerprint sensor, specifically: 2808:c652.

## Status: Incomplete

This driver is incomplete and doesn't fully work (verifying always returns score 0). I need help, so if you wanna give it a shot go ahead!

## How to run test driver with libfprint
```bash
git clone https://gitlab.freedesktop.org/libfprint/libfprint.git
cd libfprint
git clone https://github.com/s1nn3rv2/libfprint-focaltech-0xc652.git
meson setup builddir -Ddrivers=all
cd builddir
ninja
```

You can try out different functions, like enroll:
```bash
sudo examples/enroll
```

However, as mentioned earlier, verify does not work and always returns score 0. If you have any ideas on how to fix, please contribute!
```bash
sudo examples/verify
```

## How to Run (Python test script)

To run the existing testing Python script, navigate to the project directory and execute the following command:

```bash
python focaltech_test.py
```

It will generate a live image in the project directory where you can view the sensor's output data.
