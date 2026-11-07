#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "usb_config_scan.h"
static usb_transfer_t *pending;
static unsigned submits, allocs, frees;
static int fail_submit;
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
const char *esp_err_to_name(int err) { (void)err; return "mock"; }
int usb_host_transfer_alloc(size_t size,int iso,usb_transfer_t **out)
{
    (void)iso; *out=calloc(1,sizeof(**out)); (*out)->data_buffer=calloc(1,size); allocs++; return 0;
}
int usb_host_transfer_free(usb_transfer_t *t) { assert(t!=pending); free(t->data_buffer);free(t);frees++;return 0; }
int usb_host_transfer_submit_control(void *client,usb_transfer_t *t)
{
    (void)client; assert(!pending); if(fail_submit) return -1;
    usb_setup_packet_t *s=(void *)t->data_buffer;
    assert((s->bmRequestType==0x80 && s->bRequest==6 && s->wIndex==0) || (s->bmRequestType==0xc0 && s->bRequest==0x52 && s->wIndex==4));
    assert(t->num_bytes==s->wLength+8);
    if(s->bRequest==6) assert((s->wValue>>8)==2);
    pending=t;submits++;return 0;
}
static void reply(const uint8_t *data,size_t size,int status)
{
    usb_transfer_t *t=pending;assert(t);pending=NULL;
    memcpy(t->data_buffer+8,data,size);t->actual_num_bytes=size+8;t->status=status;t->callback(t);
}
static const uint8_t config[]={9,2,25,0,1,7,0,0x80,50,9,4,0,0,1,0xff,0xfe,2,0,7,5,0x81,2,0,2,0};
int main(void)
{
    /* Non-sequential bConfigurationValue is not treated as descriptor index. */
    usb_config_scan_start(NULL,(void *)1,1,2);
    assert(((usb_setup_packet_t *)pending->data_buffer)->wValue==0x200);
    reply(config,9,0);usb_config_scan_poll(1);
    reply(config,sizeof(config),0);usb_config_scan_poll(1);
    assert(((usb_setup_packet_t *)pending->data_buffer)->wValue==0x201);
    reply(config,9,0);usb_config_scan_poll(1);
    reply(config,sizeof(config),0);usb_config_scan_poll(1);
    assert(!pending && submits==4);
    uint8_t selected=99;assert(usb_config_scan_result(1,&selected) && selected==0);
    assert(usb_config_scan_request_mode(1));
    usb_setup_packet_t *setup=(usb_setup_packet_t *)pending->data_buffer;
    assert(setup->bmRequestType==0xc0 && setup->bRequest==0x52 && setup->wValue==0 && setup->wIndex==4 && setup->wLength==1);
    assert(!usb_config_scan_request_mode(1));assert(!usb_config_scan_release(1));
    reply(config,1,0);usb_config_scan_poll(1);assert(usb_config_scan_release(1));
    /* Discover a validated NCM function plus USBMUX, preserving config value. */
    uint8_t network[]={9,2,0,0,3,6,0,0x80,50,
        9,4,1,0,2,0xff,0xfe,2,0,7,5,0x84,2,0,2,0,7,5,5,2,0,2,0,
        9,4,3,0,0,2,13,0,0,5,0x24,6,3,7,
        13,0x24,0x0f,4,0,0,0,0,0xea,5,0,0,0,
        9,4,7,0,0,10,0,0,0,9,4,7,1,2,10,0,0,0,
        7,5,0x82,2,0,2,0,7,5,3,2,0,2,0};
    network[2]=sizeof(network);
    usb_config_scan_start(NULL,(void *)1,1,1);
    reply(network,9,0);usb_config_scan_poll(1);
    reply(network,sizeof(network),0);usb_config_scan_poll(1);
    assert(usb_config_scan_result(1,&selected) && selected==6);
    assert(usb_config_scan_release(1));
    /* Disconnect cannot free DMA storage before cancellation callback. */
    usb_config_scan_start(NULL,(void *)1,1,1);
    assert(!usb_config_scan_release(1));
    reply(config,0,1);assert(usb_config_scan_release(1));
    /* Timeout retains transfer; late completion does not restart scanning. */
    usb_config_scan_start(NULL,(void *)1,1,1);now+=3000001;
    usb_config_scan_poll(1);assert(!usb_config_scan_release(1));
    reply(config,9,0);usb_config_scan_poll(1);assert(!pending);assert(usb_config_scan_release(1));
    usb_config_scan_start(NULL,(void *)1,1,1);
    reply(config,8,0);usb_config_scan_poll(1);assert(!pending);assert(usb_config_scan_release(1));
    uint8_t bad[9];memcpy(bad,config,9);bad[2]=0xff;bad[3]=0xff;
    usb_config_scan_start(NULL,(void *)1,1,1);
    reply(bad,9,0);usb_config_scan_poll(1);assert(!pending);assert(usb_config_scan_release(1));
    fail_submit=1;usb_config_scan_start(NULL,(void *)1,1,1);assert(usb_config_scan_release(1));
    assert(allocs==frees);
    puts("PASS: config index/value, HS endpoint, disconnect drain, timeout/late reply, truncation, oversize, submit failure");
}
