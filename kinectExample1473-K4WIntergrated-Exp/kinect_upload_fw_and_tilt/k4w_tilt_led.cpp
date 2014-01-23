#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h> // For usleep()

uint32_t tag_seq = 1;
uint32_t tag_next_ack = 1;

typedef enum {
	LED_OFF = 1,
	LED_BLINK_GREEN = 2,
	LED_SOLID_GREEN = 3,
	LED_SOLID_RED = 4,
} led_state;

typedef struct {
	uint32_t magic;
	uint32_t tag;
	uint32_t arg1;
	uint32_t cmd;
	uint32_t arg2;
} motor_command;

typedef struct {
	uint32_t magic;
	uint32_t tag;
	uint32_t status;
} motor_reply;

#define le32(X) (X)
#define LOG(...) fprintf(stderr, __VA_ARGS__)

int get_reply(libusb_device_handle* dev){
	unsigned char buffer[512];
	memset(buffer, 0, 512);
	int transferred = 0;
	int res = 0;
	res = libusb_bulk_transfer(dev, 0x81, buffer, 512, &transferred, 0);
	if (res != 0) {
		LOG("get_reply(): libusb_bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
	} else if (transferred != 12) {
		LOG("get_reply(): weird - got %d bytes (expected 12)\n", transferred);
	} else {
		motor_reply reply;
		memcpy(&reply, buffer, sizeof(reply));
		if (reply.magic != 0x0a6fe000) {
			LOG("Bad magic: %08X (expected 0A6FE000\n", reply.magic);
			res = -1;
		}
		if (reply.tag != tag_next_ack) {
			LOG("Reply tag out of order: expected %d, got %d\n", tag_next_ack, reply.tag);
			res = -1;
		}
		if (reply.status != 0) {
			LOG("reply status != 0: failure?\n");
			res = -1;
		}
		tag_next_ack++;
		LOG("get_reply(): got %d bytes:", transferred);
		int i;
		for (i = 0; i < transferred; i++) {
			LOG(" %02X", buffer[i]);
		}
		LOG("\n");
	}
	return res;
}

int set_led(libusb_device_handle* dev, led_state state) {
	int transferred = 0;
	int res = 0;
	motor_command cmd;
	cmd.magic = le32(0x06022009);
	cmd.tag = le32(tag_seq++);
	cmd.arg1 = le32(0);
	cmd.cmd = le32(0x10);
	cmd.arg2 = (uint32_t)(le32((int32_t)state));
	unsigned char buffer[20];
	memcpy(buffer, &cmd, 20);
	// Send command to set LED to solid green
	LOG("About to send bulk transfer:");
	int i;
	for(i = 0; i < 20 ; i++) {
		LOG(" %02X", buffer[i]);
	}
	LOG("\n");
	res = libusb_bulk_transfer(dev, 0x01, buffer, 20, &transferred, 0);
	if (res != 0) {
		LOG("set_led(): libusb_bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
		return res;
	}
	return get_reply(dev);
}

int set_tilt(libusb_device_handle* dev, int tilt_degrees) {
	if (tilt_degrees > 31 || tilt_degrees < -31) {
		LOG("set_tilt(): degrees %d out of safe range [-31, 31]\n", tilt_degrees);
		return -1;
	}
	motor_command cmd;
	cmd.magic = le32(0x06022009);
	cmd.tag = le32(tag_seq++);
	cmd.arg1 = le32(0);
	cmd.cmd = le32(0x803b);
	cmd.arg2 = (uint32_t)(le32((int32_t)tilt_degrees));
	int transferred = 0;
	int res = 0;
	unsigned char buffer[20];
	memcpy(buffer, &cmd, 20);

	LOG("About to send bulk transfer:");
	int i;
	for(i = 0; i < 20 ; i++) {
		LOG(" %02X", buffer[i]);
	}
	LOG("\n");
	res = libusb_bulk_transfer(dev, 0x01, buffer, 20, &transferred, 0);
	if (res != 0) {
		LOG("set_tilt(): libusb_bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
		return res;
	}
	return get_reply(dev);
}

int poll_status(libusb_device_handle* dev) {
	int transferred = 0;
	int res = 0;
	motor_command cmd;
	cmd.magic = le32(0x06022009);
	cmd.tag = le32(tag_seq++);
	cmd.arg1 = le32(0x68); // 104.  Incidentally, the number of bytes that we expect in the reply.
	cmd.cmd = le32(0x8032);
	unsigned char buffer[256];
	memcpy(buffer, &cmd, 16);
	// Send command to set LED to solid green
	LOG("About to send bulk transfer:");
	int i;
	for(i = 0; i < 16 ; i++) {
		LOG(" %02X", buffer[i]);
	}
	LOG("\n");
	res = libusb_bulk_transfer(dev, 0x01, buffer, 16, &transferred, 0);
	if (res != 0) {
		LOG("set_led(): libusb_bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
		return res;
	}

	res = libusb_bulk_transfer(dev, 0x81, buffer, 256, &transferred, 0); // 104 bytes
	if (res != 0) {
		LOG("set_led(): libusb_bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
		return res;
	} else {
		LOG("poll_status():");
		int i;
		for(i = 0 ; i < transferred ; i += 4) {
			int32_t j;
			memcpy(&j, buffer + i, 4);
			LOG("\t%d", j);
		}
		LOG("\n");
		struct {
			int32_t x;
			int32_t y;
			int32_t z;
		} accel;
		memcpy(&accel, buffer + 16, sizeof(accel));
		LOG("X: %d\tY: %d\tZ:%d\n", accel.x, accel.y, accel.z);
	}
	// Reply: skip four uint32_t, then you have three int32_t that give you acceleration in that direction, it seems.
	// Units still to be worked out.
	return get_reply(dev);
}

int do_motor() {
	int res;
	led_state state_to_set = LED_SOLID_RED;
	int tilt = -30;

	libusb_context* ctx = NULL;
	libusb_init(&ctx);

	libusb_device_handle* dev = NULL;
	dev = libusb_open_device_with_vid_pid(ctx, 0x045e, 0x02ad);
	if (dev == NULL) {
		LOG("Failed to open audio device\n");
		libusb_exit(ctx);
		exit(1);
	}

	res = libusb_claim_interface(dev, 0);
	if (res != 0) {
		LOG("Failed to claim interface 1: %d\n", res);
		goto cleanup;
	}

	res = set_led(dev, state_to_set);
	if (res != 0) {
		LOG("set_led failed\n");
		goto cleanup;
	}

	res = set_tilt(dev, tilt);
	if (res != 0) {
		LOG("set_tilt failed\n");
		goto cleanup;
	}

	int i;
	for (i = 0; i < 10; i++) {
		res = poll_status(dev);
		if (res != 0) {
			LOG("poll_status failed\n");
			goto cleanup;
		}
		usleep(100000);
	}
	
    res = set_tilt(dev, -tilt);
	if (res != 0) {
		LOG("set_tilt failed\n");
		goto cleanup;
	}
    
cleanup:
	libusb_close(dev);
	libusb_exit(ctx);

	return 0;
}
