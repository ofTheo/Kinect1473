/*
 * Copyright 2011 Drew Fisher <drew.m.fisher@gmail.com>. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS  ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL DREW FISHER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of Drew Fisher.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libusb.h>

static int little_endian(void) {
	int i = 0;
	((char *) (&i))[0] = 1;
	return (i == 1);
}


#define maxPackSize 512


static libusb_device_handle *dev;
static unsigned int seq;

typedef struct {
	uint32_t magic;
	uint32_t seq;
	uint32_t bytes;
	uint32_t cmd;
	uint32_t write_addr;
	uint32_t unk;
} bootloader_command;

typedef struct {
	uint32_t magic;
	uint32_t seq;
	uint32_t status;
} status_code;

#define LOG(...) printf(__VA_ARGS__)

//#if __BYTE_ORDER == __BIG_ENDIAN
//static inline uint32_t fn_le32(uint32_t d)
//{
//	return (d<<24) | ((d<<8)&0xFF0000) | ((d>>8)&0xFF00) | (d>>24);
//}
//#else
#define fn_le32(x) (x)
//#endif

static void dump_bl_cmd(bootloader_command cmd) {
	int i;
	for (i = 0; i < 24; i++)
		LOG("%02X ", ((unsigned char*)(&cmd))[i]);
	LOG("\n");
}

static int get_first_reply(void) {
	unsigned char buffer[maxPackSize];
	int res;
	int transferred = 0;

	res = libusb_bulk_transfer(dev, 0x81, buffer, maxPackSize, &transferred, 10000);
	if (res != 0 ) {
		LOG("Error reading first reply: %d\ttransferred: %d (expected %d)\n", res, transferred, 0x60);
		return res;
	}
	LOG("Reading first reply: ");
	int i;
	for (i = 0; i < transferred; ++i) {
		LOG("%02X ", buffer[i]);
	}
	LOG("\n");
	return res;
}

static int get_reply(void) {
	union {
		status_code buffer;
		/* The following is needed because libusb_bulk_transfer might
		 * fail when working on a buffer smaller than maxPackSize bytes.
		 */
		unsigned char dump[maxPackSize];
	} reply;
	int res;
	int transferred = 0;

	res = libusb_bulk_transfer(dev, 0x81, reply.dump, maxPackSize, &transferred, 10000);
	if (res != 0 || transferred != sizeof(status_code)) {
		LOG("Error reading reply: %d\ttransferred: %d (expected %zu)\n", res, transferred, sizeof(status_code));
		return res;
	}
	if (fn_le32(reply.buffer.magic) != 0x0a6fe000) {
		LOG("Error reading reply: invalid magic %08X\n", reply.buffer.magic);
		return -1;
	}
	if (fn_le32(reply.buffer.seq) != seq) {
		LOG("Error reading reply: non-matching sequence number %08X (expected %08X)\n", reply.buffer.seq, seq);
		return -1;
	}
	if (fn_le32(reply.buffer.status) != 0) {
		LOG("Notice reading reply: last uint32_t was nonzero: %d\n", reply.buffer.status);
	}

	LOG("Reading reply: ");
	int i;
	for (i = 0; i < transferred; ++i) {
		LOG("%02X ", reply.dump[i]);
	}
	LOG("\n");

	return res;
}

int stallCount = 0;
void fixStall(){
//    if( stallCount > 60 ){
//        libusb_reset_device(dev);
//		libusb_set_configuration(dev, 1);
//        libusb_claim_interface(dev, 0);
//        stallCount = 0;
//    }
    //libusb_clear_halt(dev, 1);
    stallCount++;
}

int upload_main() {
	char default_filename[] = "../../../data/firmware.bin";
	char* filename = default_filename;
	int res = 0;

	FILE* fw = fopen(filename, "rb");
	if (fw == NULL) {
		fprintf(stderr, "Failed to open %s: %s\n", filename, strerror(errno));
		return errno;
	}

	libusb_init(NULL);
	libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_INFO);
    
	dev = libusb_open_device_with_vid_pid(NULL, 0x045e, 0x02ad);
	if (dev == NULL) {
		fprintf(stderr, "Couldn't open device.\n");
		res = -ENODEV;
		//goto fail_libusb_open;
        libusb_exit(NULL);
        fclose(fw);
        printf("can't open device\n");
        return;
    }

	int current_configuration = 0;
	libusb_get_configuration(dev, &current_configuration);
    
    printf("current config is %i\n", current_configuration);
    
	if (current_configuration != 1)
		libusb_set_configuration(dev, 1);

	libusb_get_configuration(dev, &current_configuration);
    printf("current config is now %i\n", current_configuration);
//
//    libusb_reset_device(dev);

	int error = libusb_claim_interface(dev, 0);
    if( error != 0){
        printf("could not libusb_claim_interface\n");
    }
    
	libusb_get_configuration(dev, &current_configuration);
	if (current_configuration != 1) {
		res = -ENODEV;
		//goto cleanup;
       	libusb_close(dev);
        printf("can't get config\n");
        return;
	}

	seq = 1;

	bootloader_command cmd;
	cmd.magic = fn_le32(0x06022009);
	cmd.seq = fn_le32(seq);
	cmd.bytes = fn_le32(0x60);
	cmd.cmd = fn_le32(0);
	cmd.write_addr = fn_le32(0x15);
	cmd.unk = fn_le32(0);

	LOG("About to send: ");
	dump_bl_cmd(cmd);

	int transferred = 0;
    
    fixStall();
	res = libusb_bulk_transfer(dev, 1, (unsigned char*)&cmd, sizeof(cmd), &transferred, 0);
	if (res != 0 || transferred != sizeof(cmd)) {
		LOG("Error: res: %d\ttransferred: %d (expected %zu)\n", res, transferred, sizeof(cmd));
		//goto cleanup;
       	libusb_close(dev);
        printf("can't do libusb_bulk_transfer\n");
        return;
	}else{
        printf("success transferred %i\n", transferred);
    }
    
    fixStall();

	res = get_first_reply(); // This first one doesn't have the usual magic bytes at the beginning, and is 96 bytes long - much longer than the usual 12-byte replies.
	res = get_reply(); // I'm not sure why we do this twice here, but maybe it'll make sense later.
	seq++;
    

    
	// Split addr declaration and assignment in order to compile as C++,
	// otherwise this would give "jump to label '...' crosses initialization"
	// errors.
	uint32_t addr;
	addr = 0x00080000;
    
    int lSize;
    fseek (fw , 0 , SEEK_END);
    lSize = ftell (fw);
    rewind (fw);
    
	unsigned char page[lSize];

    printf("firmware size is %i\n", lSize); 
    
	int read;
	do {
        if( read == lSize ){
            break;
        }
        
		read = fread(page, 1, lSize, fw);
        printf("fread read is %i\n", read);

		if (read <= 0) {
			break;
		}
        
        
		//LOG("");
		cmd.seq = fn_le32(seq);
		cmd.bytes = fn_le32(read);
		cmd.cmd = fn_le32(0x03);
		cmd.write_addr = fn_le32(addr);
		LOG("About to send: ");
		dump_bl_cmd(cmd);
		// Send it off!
		transferred = 0;
        
        fixStall();
		res = libusb_bulk_transfer(dev, 1, (unsigned char*)&cmd, sizeof(cmd), &transferred, 0);
        while( res == -9 ){
            fixStall();
            res = libusb_bulk_transfer(dev, 1, (unsigned char*)&cmd, sizeof(cmd), &transferred, 0);
        }
        
		if (res != 0 || transferred != sizeof(cmd)) {
			LOG("Error 2: res: %d\ttransferred: %d (expected %zu)\n", res, transferred, sizeof(cmd));
			//goto cleanup;
           	libusb_close(dev);
        }
		int bytes_sent = 0;

		while (bytes_sent < read) {
			int to_send = (read - bytes_sent > maxPackSize ? maxPackSize : read - bytes_sent);
			transferred = 0;
            
			res = libusb_bulk_transfer(dev, 1, &page[bytes_sent], to_send, &transferred, 0);
            while( res == -9 ){
                printf("clearing halt\n"); 
                fixStall();
                usleep(10);
                res = libusb_bulk_transfer(dev, 1, &page[bytes_sent], to_send, &transferred, 0);
            }
            
            if( res != -9 ){
                if (res != 0 || transferred != to_send) {
                    LOG("Error 3: res: %d\ttransferred: %d (expected %d)\n", res, transferred, to_send);
                    //goto cleanup;
                    libusb_close(dev);
                    return; 
                }
                bytes_sent += to_send;
            }
            printf("bytes_sent is: %i\n", bytes_sent);
            printf("transferred is: %i\n", transferred);

		}
		
        res = get_reply();
        
        printf("bytes_sent is: %i\n", bytes_sent);
        printf("read is: %i\n", read);
        
        addr += (uint32_t)read;
		seq++;
	} while (read > 0);

	cmd.seq = fn_le32(seq);
	cmd.bytes = fn_le32(0);
	cmd.cmd = fn_le32(0x04);
	cmd.write_addr = fn_le32(0x00080030);
	dump_bl_cmd(cmd);
	transferred = 0;
 
	res = libusb_bulk_transfer(dev, 1, (unsigned char*)&cmd, sizeof(cmd), &transferred, 0);
    while( res == -9 ){
        fixStall();
        res = libusb_bulk_transfer(dev, 1, (unsigned char*)&cmd, sizeof(cmd), &transferred, 0);
    }
    
	if (res != 0 || transferred != sizeof(cmd)) {
		LOG("Error: res: %d\ttransferred: %d (expected %zu)\n", res, transferred, sizeof(cmd));
		//goto cleanup;
		libusb_close(dev);
        return;
    }
    
    printf("read is: %i\n", read);

	res = get_reply();
	seq++;
	// Now the device reenumerates.
    
    
    
    printf("reached end\n");
    libusb_close(dev);
}
