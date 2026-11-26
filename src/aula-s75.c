// aula-s75 — set the screen clock and read the battery of an AULA S75 Pro on macOS.
//
// Talks to the keyboard over its USB cable or its 2.4 GHz dongle using IOKit only.
// See docs/PROTOCOL.md for the packets.
//
// SPDX-License-Identifier: MIT

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define VERSION "0.1.0"

// USB cable: SONiX vendor collection, 64-byte feature reports, no report ID.
#define USB_VID 0x0C45
#define USB_PID 0x800A
#define USB_PAGE 0xFF13
#define USB_PKT 64
#define USB_DELAY_US 35000

// 2.4 GHz dongle: vendor collection, 32-byte output and input reports, no report ID.
#define RF_VID 0x05AC
#define RF_PID 0x024F
#define RF_PAGE 0xFF60
#define RF_PKT 32
#define RF_TIMEOUT_S 1.0

enum { EXIT_OK = 0, EXIT_FAIL = 1, EXIT_USAGE = 2, EXIT_NODEV = 3 };
enum transport { T_AUTO, T_USB, T_DONGLE };

static int verbose = 0;
static int dry_run = 0;

static void dump(const char *dir, const uint8_t *b, int n) {
    if (!verbose && !dry_run) return;
    fprintf(stderr, "%s", dir);
    for (int i = 0; i < n; i++) fprintf(stderr, " %02x", b[i]);
    fprintf(stderr, "\n");
}

// ---------------------------------------------------------------- device lookup

static void cfnum(CFMutableDictionaryRef d, const char *key, int v) {
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &v);
    CFDictionarySetValue(d, k, n);
    CFRelease(k);
    CFRelease(n);
}

static IOHIDDeviceRef find_device(int vid, int pid, int page) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    CFMutableDictionaryRef m = CFDictionaryCreateMutable(
        NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    cfnum(m, kIOHIDVendorIDKey, vid);
    cfnum(m, kIOHIDProductIDKey, pid);
    cfnum(m, kIOHIDPrimaryUsagePageKey, page);
    IOHIDManagerSetDeviceMatching(mgr, m);
    CFRelease(m);

    IOHIDDeviceRef dev = NULL;
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (set && CFSetGetCount(set) > 0) {
        CFIndex n = CFSetGetCount(set);
        const void **devs = calloc((size_t)n, sizeof *devs);
        CFSetGetValues(set, devs);
        dev = (IOHIDDeviceRef)CFRetain(devs[0]);
        free(devs);
    }
    if (set) CFRelease(set);
    CFRelease(mgr);
    return dev;
}

static int open_device(IOHIDDeviceRef dev) {
    IOReturn r = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
    if (r != kIOReturnSuccess) {
        fprintf(stderr, "error: could not open the device (0x%08x)\n", r);
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------- clock payload

// 00 01 5a YY MM DD hh mm ss 00 WD   (WD: 0 = Sunday)
static void clock_fields(uint8_t *p, const struct tm *t) {
    p[0] = 0x00;
    p[1] = 0x01;  // slot
    p[2] = 0x5A;
    p[3] = (uint8_t)(t->tm_year + 1900 - 2000);
    p[4] = (uint8_t)(t->tm_mon + 1);
    p[5] = (uint8_t)t->tm_mday;
    p[6] = (uint8_t)t->tm_hour;
    p[7] = (uint8_t)t->tm_min;
    p[8] = (uint8_t)t->tm_sec;
    p[9] = 0x00;
    p[10] = (uint8_t)t->tm_wday;
}

// ---------------------------------------------------------------- USB transport

static void usb_command(uint8_t *p, uint8_t op, uint8_t count) {
    memset(p, 0, USB_PKT);
    p[0] = 0x04;
    p[1] = op;
    p[8] = count;
}

static int usb_send(IOHIDDeviceRef dev, const uint8_t *b) {
    usleep(USB_DELAY_US);
    dump("->", b, verbose ? USB_PKT : 16);
    if (dry_run) return 1;
    IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, 0, b, USB_PKT);
    if (r != kIOReturnSuccess) {
        fprintf(stderr, "error: SET_REPORT failed (0x%08x)\n", r);
        return 0;
    }
    return 1;
}

static int usb_read(IOHIDDeviceRef dev, uint8_t *b) {
    memset(b, 0, USB_PKT);
    if (dry_run) return 1;
    usleep(USB_DELAY_US);
    CFIndex len = USB_PKT;
    IOReturn r = IOHIDDeviceGetReport(dev, kIOHIDReportTypeFeature, 0, b, &len);
    if (r != kIOReturnSuccess) {
        fprintf(stderr, "error: GET_REPORT failed (0x%08x)\n", r);
        return 0;
    }
    dump("<-", b, 16);
    return 1;
}

// Sends a control command and checks the ack flag (reply[3] == 0x01).
static int usb_ack(IOHIDDeviceRef dev, uint8_t op, uint8_t count) {
    uint8_t cmd[USB_PKT], reply[USB_PKT];
    usb_command(cmd, op, count);
    if (!usb_send(dev, cmd) || !usb_read(dev, reply)) return 0;
    if (dry_run) return 1;
    if (reply[1] != op || reply[3] != 0x01) {
        fprintf(stderr, "error: no ack for command %02x (reply %02x %02x %02x %02x)\n",
                op, reply[0], reply[1], reply[2], reply[3]);
        return 0;
    }
    return 1;
}

static int usb_clock(IOHIDDeviceRef dev, const struct tm *t) {
    uint8_t data[USB_PKT], echo[USB_PKT];
    memset(data, 0, sizeof data);
    clock_fields(data, t);
    data[62] = 0xAA;
    data[63] = 0x55;

    if (!usb_ack(dev, 0x18, 0)) return 0;        // begin session
    if (!usb_ack(dev, 0x28, 1)) return 0;        // clock, 1 data packet follows
    if (!usb_send(dev, data)) return 0;
    if (!usb_read(dev, echo)) return 0;          // echoes the data, no ack flag
    return usb_ack(dev, 0x02, 0);                // end session, save
}

// ---------------------------------------------------------------- dongle transport

static uint8_t rf_inbuf[64];
static uint8_t rf_want[3];
static uint8_t rf_reply[RF_PKT];
static int rf_got = 0;

static void rf_on_input(void *ctx, IOReturn res, void *sender, IOHIDReportType type,
                        uint32_t id, uint8_t *report, CFIndex len) {
    (void)ctx; (void)res; (void)sender; (void)type; (void)id;
    int n = len < RF_PKT ? (int)len : RF_PKT;
    dump("<-", report, n);
    if (n >= 3 && memcmp(report, rf_want, 3) == 0) {
        memcpy(rf_reply, report, (size_t)n);
        rf_got = 1;
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
}

// cmd len seq data[28] checksum   (checksum = sum of the first 31 bytes, mod 256)
static void rf_frame(uint8_t *f, uint8_t cmd, uint8_t len, const uint8_t *data) {
    memset(f, 0, RF_PKT);
    f[0] = cmd;
    f[1] = len;
    f[2] = 0;
    if (data) memcpy(f + 3, data, 28);
    unsigned sum = 0;
    for (int i = 0; i < RF_PKT - 1; i++) sum += f[i];
    f[RF_PKT - 1] = (uint8_t)(sum & 0xFF);
}

static void rf_attach(IOHIDDeviceRef dev) {
    IOHIDDeviceScheduleWithRunLoop(dev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(dev, rf_inbuf, sizeof rf_inbuf, rf_on_input, NULL);
}

// Sends a frame and waits for the keyboard to echo cmd/len/seq. Returns 0 on timeout.
static int rf_xfer(IOHIDDeviceRef dev, const uint8_t *f) {
    dump("->", f, RF_PKT);
    if (dry_run) return 1;
    memcpy(rf_want, f, 3);
    rf_got = 0;
    IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, f, RF_PKT);
    if (r != kIOReturnSuccess) {
        fprintf(stderr, "error: SET_REPORT failed (0x%08x)\n", r);
        return 0;
    }
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + RF_TIMEOUT_S;
    while (!rf_got && CFAbsoluteTimeGetCurrent() < deadline)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, deadline - CFAbsoluteTimeGetCurrent(), false);
    return rf_got;
}

static int rf_clock(IOHIDDeviceRef dev, const struct tm *t) {
    uint8_t data[28] = {0}, f[RF_PKT];
    clock_fields(data, t);
    data[14] = 0xAA;
    data[15] = 0x55;
    rf_frame(f, 0x0C, 0x10, data);
    return rf_xfer(dev, f);
}

// Returns the battery percentage, 0 when the keyboard reports no reading, -1 on timeout.
static int rf_battery(IOHIDDeviceRef dev) {
    uint8_t f[RF_PKT];
    rf_frame(f, 0x20, 0x01, NULL);
    if (!rf_xfer(dev, f)) return -1;
    if (dry_run) return 0;
    return rf_reply[3] > 100 ? 100 : rf_reply[3];
}

// ---------------------------------------------------------------- commands

static const char *NO_REPLY =
    "error: the keyboard did not answer through the dongle; "
    "it may be asleep (press a key), off, or not in 2.4G mode\n";

static int cmd_clock(enum transport tr) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char when[64];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S %a", &t);

    if (tr != T_DONGLE) {
        IOHIDDeviceRef dev = dry_run ? NULL : find_device(USB_VID, USB_PID, USB_PAGE);
        if (dev || dry_run) {
            if (dev && !open_device(dev)) return EXIT_FAIL;
            int ok = usb_clock(dev, &t);
            if (dev) IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
            if (!ok) return EXIT_FAIL;
            printf("%sclock set to %s (usb)\n", dry_run ? "[dry run] " : "", when);
            if (!dry_run || tr == T_USB) return EXIT_OK;
        } else if (tr == T_USB) {
            fprintf(stderr, "error: keyboard not found on USB (%04x:%04x)\n", USB_VID, USB_PID);
            return EXIT_NODEV;
        }
    }

    IOHIDDeviceRef dev = dry_run ? NULL : find_device(RF_VID, RF_PID, RF_PAGE);
    if (!dev && !dry_run) {
        fprintf(stderr, "error: keyboard not found on USB or through the 2.4G dongle\n");
        return EXIT_NODEV;
    }
    if (dev) {
        if (!open_device(dev)) return EXIT_FAIL;
        rf_attach(dev);
    }
    int ok = rf_clock(dev, &t);
    if (dev) IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
    if (!ok) { fputs(NO_REPLY, stderr); return EXIT_FAIL; }
    printf("%sclock set to %s (2.4g)\n", dry_run ? "[dry run] " : "", when);
    return EXIT_OK;
}

static int cmd_battery(void) {
    IOHIDDeviceRef dev = dry_run ? NULL : find_device(RF_VID, RF_PID, RF_PAGE);
    if (!dev && !dry_run) {
        fprintf(stderr, "error: 2.4G dongle not found; the level is only reported over 2.4G\n");
        return EXIT_NODEV;
    }
    if (dev) {
        if (!open_device(dev)) return EXIT_FAIL;
        rf_attach(dev);
    }
    int pct = rf_battery(dev);
    if (dev) IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
    if (dry_run) return EXIT_OK;
    if (pct < 0) {
        IOHIDDeviceRef usb = find_device(USB_VID, USB_PID, USB_PAGE);
        if (usb) {
            CFRelease(usb);
            fprintf(stderr, "error: the keyboard is on the USB cable; "
                            "it only reports its battery level in 2.4G mode\n");
        } else {
            fputs(NO_REPLY, stderr);
        }
        return EXIT_FAIL;
    }
    if (pct == 0) printf("battery: 0%% (the keyboard reports no level, or the battery is empty)\n");
    else printf("battery: %d%%\n", pct);
    return EXIT_OK;
}

static void usage(FILE *out) {
    fprintf(out,
            "usage: aula-s75 <command> [options]\n"
            "\n"
            "commands:\n"
            "  clock      set the screen clock to the Mac's local time\n"
            "  battery    read the battery level (2.4G mode only)\n"
            "  version    print the version\n"
            "\n"
            "options:\n"
            "  --usb      clock: use the USB cable only\n"
            "  --dongle   clock: use the 2.4G dongle only\n"
            "  --dry-run  print the packets without sending them\n"
            "  -v         print every packet sent and received\n");
}

int main(int argc, char **argv) {
    const char *cmd = NULL;
    enum transport tr = T_AUTO;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) verbose = 1;
        else if (!strcmp(a, "--dry-run")) dry_run = 1;
        else if (!strcmp(a, "--usb")) tr = T_USB;
        else if (!strcmp(a, "--dongle")) tr = T_DONGLE;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return EXIT_OK; }
        else if (a[0] == '-' || cmd) { usage(stderr); return EXIT_USAGE; }
        else cmd = a;
    }

    if (!cmd) { usage(stderr); return EXIT_USAGE; }
    if (!strcmp(cmd, "clock")) return cmd_clock(tr);
    if (!strcmp(cmd, "battery")) return cmd_battery();
    if (!strcmp(cmd, "version")) { printf("aula-s75 %s\n", VERSION); return EXIT_OK; }
    usage(stderr);
    return EXIT_USAGE;
}
