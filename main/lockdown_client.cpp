// SPDX-License-Identifier: GPL-3.0-or-later
// Wired flow and pair-record certificate profile follow local DiPlay transport.
#include "lockdown_client.h"
#include "app_session.h"
#include "iap2_client.h"
#include "lockdown_xml.h"
#include "mux_channels.h"
#include "nvs_flash.h"
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "bootloader_random.h"
#include "entropy_source.h"
#ifdef TAB5_SOFTWARE_AUTH
extern "C" {
#include "auth_software_provider.h"
}
#endif
#include "nvs.h"
#include "mbedtls/base64.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char tag[]="LOCKDOWN";
constexpr uint32_t legacy_record_magic=0x43505231, record_magic=0x43505232;
// Owned by the single Lockdown worker; cleared for every connection.
bool storage_failed;
struct PairRecord {
    uint32_t magic;
    uint8_t trusted;
    uint8_t phone_key_hash[32];
    char host_id[37], buid[37];
    char root_key[2048], root_cert[1536], host_cert[1536], device_cert[1536];
};
int random_bytes(void *,unsigned char *out,size_t n) { esp_fill_random(out,n); return 0; }
void uuid(char out[37]) {
    uint8_t bytes[16]; esp_fill_random(bytes,sizeof(bytes)); bytes[6]=(bytes[6]&15)|64; bytes[8]=(bytes[8]&63)|128;
    constexpr char hex[]="0123456789ABCDEF";
    unsigned pos=0;
    for (unsigned i=0;i<16;++i) {
        if (i==4 || i==6 || i==8 || i==10) out[pos++]='-';
        out[pos++]=hex[bytes[i]>>4]; out[pos++]=hex[bytes[i]&15];
    }
    out[pos]=0;
}
bool key_hash(mbedtls_pk_context *key,uint8_t out[32]) {
    uint8_t der[1024]; int n=mbedtls_pk_write_pubkey_der(key,der,sizeof(der));
    return n>0 && mbedtls_sha256(der+sizeof(der)-n,n,out,0)==0;
}
bool valid_record(const PairRecord &r) {
    return (r.magic==record_magic || r.magic==legacy_record_magic) && r.trusted<=1 &&
        strnlen(r.host_id,sizeof(r.host_id))==36 && strnlen(r.buid,sizeof(r.buid))==36 &&
        strnlen(r.root_key,sizeof(r.root_key))<sizeof(r.root_key) &&
        strnlen(r.root_cert,sizeof(r.root_cert))<sizeof(r.root_cert) &&
        strnlen(r.host_cert,sizeof(r.host_cert))<sizeof(r.host_cert) &&
        strnlen(r.device_cert,sizeof(r.device_cert))<sizeof(r.device_cert) && r.root_key[0] && r.root_cert[0];
}
bool save(const PairRecord &r);
bool load(PairRecord &r) {
    nvs_handle_t h;
    if (nvs_open("tab5_pair",NVS_READONLY,&h)!=ESP_OK) return false;
    size_t n=sizeof(r); esp_err_t e=nvs_get_blob(h,"record",&r,&n); nvs_close(h);
    return e==ESP_OK && n==sizeof(r) && valid_record(r);
}
void record_key(const uint8_t hash[32], char key[16]) {
    constexpr char hex[]="0123456789abcdef";
    key[0]='p';
    for (unsigned i=0;i<7;i++) { key[1+i*2]=hex[hash[i]>>4]; key[2+i*2]=hex[hash[i]&15]; }
    key[15]=0;
}
bool load_phone(PairRecord &r,const uint8_t hash[32]) {
    char key[16]; record_key(hash,key);
    nvs_handle_t h;
    if(nvs_open("tab5_pair",NVS_READONLY,&h)!=ESP_OK) return false;
    size_t n=sizeof(r); esp_err_t e=nvs_get_blob(h,key,&r,&n); nvs_close(h);
    return e==ESP_OK && n==sizeof(r) && valid_record(r) && !memcmp(r.phone_key_hash,hash,32);
}
bool save(const PairRecord &r) {
    char key[16]; record_key(r.phone_key_hash,key);
    nvs_handle_t h;
    esp_err_t e=nvs_open("tab5_pair",NVS_READWRITE,&h);
    if(e==ESP_OK) {
        auto prior=std::make_unique<PairRecord>();
        size_t n=sizeof(*prior);
        esp_err_t existing=nvs_get_blob(h,key,prior.get(),&n);
        if(existing!=ESP_ERR_NVS_NOT_FOUND &&
           (existing!=ESP_OK || n!=sizeof(*prior) || !valid_record(*prior) || memcmp(prior->phone_key_hash,r.phone_key_hash,32))) {
            ESP_LOGE(tag,"Pair record collision/corruption; refusing overwrite");
            mbedtls_platform_zeroize(prior.get(),sizeof(*prior)); nvs_close(h); return false;
        }
        mbedtls_platform_zeroize(prior.get(),sizeof(*prior));
        e=nvs_set_blob(h,key,&r,sizeof(r));
        if(e==ESP_OK) e=nvs_commit(h);
        nvs_close(h);
    }
    if(e!=ESP_OK) {
        storage_failed=true;
        ESP_LOGE(tag,"Pair persistence failed: %s; existing NVS is not erased",esp_err_to_name(e));
    }
    return e==ESP_OK;
}
struct Keys {
    mbedtls_pk_context root,host,device;
    Keys() { mbedtls_pk_init(&root); mbedtls_pk_init(&host); mbedtls_pk_init(&device); }
    ~Keys() { mbedtls_pk_free(&root); mbedtls_pk_free(&host); mbedtls_pk_free(&device); }
};
bool certificate(mbedtls_pk_context *subject,mbedtls_pk_context *root,const char *subject_name,bool ca,bool ski,char *out,size_t size) {
    mbedtls_x509write_cert crt; mbedtls_x509write_crt_init(&crt);
    mbedtls_x509write_crt_set_version(&crt,MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt,MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt,subject);
    mbedtls_x509write_crt_set_issuer_key(&crt,root);
    // mbedTLS rejects Android's empty issuer Name. Use explicit names while
    // preserving the RSA keys and host identifiers when migrating that profile.
    unsigned char serial=0;
    int rc=mbedtls_x509write_crt_set_issuer_name(&crt,"CN=CarPlay_Tab5 Root");
    if (!rc) rc=mbedtls_x509write_crt_set_subject_name(&crt,subject_name);
    if (!rc) rc=mbedtls_x509write_crt_set_serial_raw(&crt,&serial,1);
    if (!rc) rc=mbedtls_x509write_crt_set_validity(&crt,"20250101000000","20360101000000");
    if (!rc) rc=mbedtls_x509write_crt_set_basic_constraints(&crt,ca,-1);
    if (!rc && !ca) rc=mbedtls_x509write_crt_set_key_usage(&crt,MBEDTLS_X509_KU_DIGITAL_SIGNATURE|MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
    if (!rc && ski) rc=mbedtls_x509write_crt_set_subject_key_identifier(&crt);
    if (!rc) rc=mbedtls_x509write_crt_pem(&crt,reinterpret_cast<uint8_t *>(out),size,random_bytes,nullptr);
    mbedtls_x509write_crt_free(&crt);
    if (rc) ESP_LOGE(tag,"Pair certificate generation failed: -0x%x",-rc);
    return rc==0;
}
bool make_record(PairRecord &r,const std::string &phone_public_key) {
    Keys keys;
    int rc=mbedtls_pk_parse_public_key(&keys.device,reinterpret_cast<const uint8_t *>(phone_public_key.c_str()),phone_public_key.size()+1);
    if (rc || !mbedtls_pk_can_do(&keys.device,MBEDTLS_PK_RSA) || mbedtls_pk_get_bitlen(&keys.device)!=2048) {
        ESP_LOGE(tag,"Unsupported phone pairing public key"); return false;
    }
    uint8_t hash[32]; if (!key_hash(&keys.device,hash)) return false;
    char host_id[37]={}, buid[37]={};
    bool reuse=valid_record(r) && !memcmp(hash,r.phone_key_hash,32) &&
        mbedtls_pk_parse_key(&keys.root,reinterpret_cast<const uint8_t *>(r.root_key),strlen(r.root_key)+1,nullptr,0,random_bytes,nullptr)==0;
    if (reuse) { memcpy(host_id,r.host_id,sizeof(host_id)); memcpy(buid,r.buid,sizeof(buid)); }
    memset(&r,0,sizeof(r)); r.magic=record_magic;
    if (reuse) { memcpy(r.host_id,host_id,sizeof(host_id)); memcpy(r.buid,buid,sizeof(buid)); }
    else { uuid(r.host_id); uuid(r.buid); }
    memcpy(r.phone_key_hash,hash,32);
    ESP_LOGI(tag,"Generating local RSA pair identity; keep phone connected");
    rc=0;
    if (!reuse) {
        rc=mbedtls_pk_setup(&keys.root,mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
        if (!rc) rc=mbedtls_rsa_gen_key(mbedtls_pk_rsa(keys.root),random_bytes,nullptr,2048,65537);
    }
    vTaskDelay(1);
    if (!rc) rc=mbedtls_pk_setup(&keys.host,mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (!rc) rc=mbedtls_rsa_gen_key(mbedtls_pk_rsa(keys.host),random_bytes,nullptr,2048,65537);
    if (!rc) rc=mbedtls_pk_write_key_pem(&keys.root,reinterpret_cast<uint8_t *>(r.root_key),sizeof(r.root_key));
    if (rc) { ESP_LOGE(tag,"RSA pair identity generation failed: -0x%x",-rc); return false; }
    if (!certificate(&keys.root,&keys.root,"CN=CarPlay_Tab5 Root",true,false,r.root_cert,sizeof(r.root_cert)) ||
        !certificate(&keys.host,&keys.root,"CN=CarPlay_Tab5 Host",false,false,r.host_cert,sizeof(r.host_cert)) ||
        !certificate(&keys.device,&keys.root,"CN=CarPlay_Tab5 Phone",false,true,r.device_cert,sizeof(r.device_cert))) return false;
    for (const char *pem:{r.root_cert,r.host_cert,r.device_cert}) {
        mbedtls_x509_crt check; mbedtls_x509_crt_init(&check);
        rc=mbedtls_x509_crt_parse(&check,reinterpret_cast<const uint8_t *>(pem),strlen(pem)+1);
        mbedtls_x509_crt_free(&check);
        if (rc) { ESP_LOGE(tag,"Generated certificate self-check failed: -0x%x",-rc); return false; }
    }
    ESP_LOGI(tag,"Local pair identity generated; no key material is logged");
    return save(r);
}

using namespace lockdown_xml;
std::string data_element(const char *key,const char *pem) {
    size_t n=strlen(pem), written=0;
    std::vector<uint8_t> encoded((n+2)/3*4+1);
    if (mbedtls_base64_encode(encoded.data(),encoded.size(),&written,reinterpret_cast<const uint8_t *>(pem),n)!=0) return {};
    return std::string("<key>")+key+"</key><data>"+std::string(reinterpret_cast<char *>(encoded.data()),written)+"</data>";
}
std::string wrap(const std::string &body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"><dict>"+body+"</dict></plist>";
}
constexpr char label[]="<key>Label</key><string>CarPlay_Tab5</string>";

class Stream {
public:
    uint32_t token;
    explicit Stream(uint32_t value):token(value) {}
    virtual ~Stream() { if (token) mux_stream_close(token); }
    virtual int read(uint8_t *out,size_t size,unsigned timeout_ms) { return mux_stream_read(token,out,size,timeout_ms); }
    virtual bool write(const uint8_t *data,size_t size) { return mux_stream_write(token,data,size); }
    bool exact(uint8_t *out,size_t size,unsigned timeout_ms) {
        int64_t end=esp_timer_get_time()+int64_t(timeout_ms)*1000;
        size_t pos=0;
        while (pos<size && esp_timer_get_time()<end) {
            int n=read(out+pos,size-pos,100);
            if (n<0) return false;
            pos+=n;
        }
        return pos==size;
    }
};
bool request(Stream &stream,const std::string &body,std::string &reply,unsigned timeout=10000) {
    std::string xml=wrap(body);
    uint32_t size=xml.size(); uint8_t header[4]={uint8_t(size>>24),uint8_t(size>>16),uint8_t(size>>8),uint8_t(size)};
    if (!stream.write(header,4) || !stream.write(reinterpret_cast<const uint8_t *>(xml.data()),xml.size()) || !stream.exact(header,4,timeout)) {
        ESP_LOGE(tag,"Plist request transport failed"); return false;
    }
    size=uint32_t(header[0])<<24|uint32_t(header[1])<<16|uint32_t(header[2])<<8|header[3];
    if (!size || size>32768) { ESP_LOGE(tag,"Invalid plist response length"); return false; }
    reply.resize(size);
    if (!stream.exact(reinterpret_cast<uint8_t *>(reply.data()),size,timeout) || reply.find("</plist>")==std::string::npos) {
        ESP_LOGE(tag,"Incomplete or unsupported plist response"); return false;
    }
    return true;
}
bool set_buid(Stream &stream,const PairRecord &r) {
    std::string reply;
    return request(stream,std::string(label)+"<key>Request</key><string>SetValue</string><key>Key</key><string>UntrustedHostBUID</string><key>Value</key><string>"+r.buid+"</string>",reply) && error_value(reply).empty();
}
bool get_public_key(Stream &stream,std::string &key) {
    std::string reply, encoded;
    if (!request(stream,std::string(label)+"<key>Request</key><string>GetValue</string><key>Key</key><string>DevicePublicKey</string>",reply) ||
        !error_value(reply).empty() || !text_value(reply,"Value","data",encoded)) {
        ESP_LOGE(tag,"DevicePublicKey unavailable; phone must be unlocked"); return false;
    }
    std::string clean; for (char c:encoded) if (!whitespace(c)) clean+=c;
    std::vector<uint8_t> decoded(clean.size()+1); size_t n=0;
    if (mbedtls_base64_decode(decoded.data(),decoded.size(),&n,reinterpret_cast<const uint8_t *>(clean.data()),clean.size())!=0) return false;
    key.assign(reinterpret_cast<char *>(decoded.data()),n); return true;
}

// Written by the heap failure hook, which can run in any task or ISR, so it only
// records; the Lockdown worker reports after the failing handshake step returns.
struct AllocFailures {
    uint32_t count, size, caps;
    const char *function;
};
DRAM_ATTR AllocFailures alloc_failures;
IRAM_ATTR void note_alloc_failure(size_t size,uint32_t caps,const char *function) {
    __atomic_store_n(&alloc_failures.size,uint32_t(size),__ATOMIC_RELAXED);
    __atomic_store_n(&alloc_failures.caps,caps,__ATOMIC_RELAXED);
    __atomic_store_n(&alloc_failures.function,function,__ATOMIC_RELAXED);
    __atomic_fetch_add(&alloc_failures.count,1,__ATOMIC_RELEASE);
}
uint32_t alloc_failure_count() { return __atomic_load_n(&alloc_failures.count,__ATOMIC_ACQUIRE); }
void log_alloc_failures(const char *name,uint32_t since) {
    uint32_t count=alloc_failure_count();
    if (count==since) return;
    const char *function=__atomic_load_n(&alloc_failures.function,__ATOMIC_RELAXED);
    ESP_LOGE(tag,"TLS_ALLOC_FAILED %s count=%u last_size=%u caps=0x%x in=%s",name,(unsigned)(count-since),
             (unsigned)__atomic_load_n(&alloc_failures.size,__ATOMIC_RELAXED),
             (unsigned)__atomic_load_n(&alloc_failures.caps,__ATOMIC_RELAXED),function?function:"?");
}
// MALLOC_CAP_INTERNAL also counts LP RAM and TCM, neither of which is DMA capable,
// so report them apart: AES/SHA DMA needs dma_free, not internal_free.
constexpr uint32_t dma_caps=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL;
void log_tls_heap(const char *name,const char *stage) {
    ESP_LOGI(tag,"TLS_HEAP %s %s dma_free=%u dma_largest=%u dma_min=%u internal_free=%u lp_free=%u tcm_free=%u "
             "psram_free=%u psram_largest=%u stack_free=%u alloc_fail=%u",name,stage,
             (unsigned)heap_caps_get_free_size(dma_caps),(unsigned)heap_caps_get_largest_free_block(dma_caps),
             (unsigned)heap_caps_get_minimum_free_size(dma_caps),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_RTCRAM),(unsigned)heap_caps_get_free_size(MALLOC_CAP_TCM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr),(unsigned)alloc_failure_count());
}
const char *tls_state_name(int state) {
    switch (state) {
        case MBEDTLS_SSL_HELLO_REQUEST: return "HELLO_REQUEST";
        case MBEDTLS_SSL_CLIENT_HELLO: return "CLIENT_HELLO";
        case MBEDTLS_SSL_SERVER_HELLO: return "SERVER_HELLO";
        case MBEDTLS_SSL_SERVER_CERTIFICATE: return "SERVER_CERTIFICATE";
        case MBEDTLS_SSL_SERVER_KEY_EXCHANGE: return "SERVER_KEY_EXCHANGE";
        case MBEDTLS_SSL_CERTIFICATE_REQUEST: return "CERTIFICATE_REQUEST";
        case MBEDTLS_SSL_SERVER_HELLO_DONE: return "SERVER_HELLO_DONE";
        case MBEDTLS_SSL_CLIENT_CERTIFICATE: return "CLIENT_CERTIFICATE";
        case MBEDTLS_SSL_CLIENT_KEY_EXCHANGE: return "CLIENT_KEY_EXCHANGE";
        case MBEDTLS_SSL_CERTIFICATE_VERIFY: return "CERTIFICATE_VERIFY";
        case MBEDTLS_SSL_CLIENT_CHANGE_CIPHER_SPEC: return "CLIENT_CHANGE_CIPHER_SPEC";
        case MBEDTLS_SSL_CLIENT_FINISHED: return "CLIENT_FINISHED";
        case MBEDTLS_SSL_SERVER_CHANGE_CIPHER_SPEC: return "SERVER_CHANGE_CIPHER_SPEC";
        case MBEDTLS_SSL_SERVER_FINISHED: return "SERVER_FINISHED";
        case MBEDTLS_SSL_FLUSH_BUFFERS: return "FLUSH_BUFFERS";
        case MBEDTLS_SSL_HANDSHAKE_WRAPUP: return "HANDSHAKE_WRAPUP";
        default: return "OTHER";
    }
}

class TlsStream final : public Stream {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    uint8_t expected_key[32];
    static int send_cb(void *ctx,const uint8_t *data,size_t size) {
        auto *self=static_cast<TlsStream *>(ctx);
        return mux_stream_write(self->token,data,size)?int(size):MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    static int recv_cb(void *ctx,uint8_t *data,size_t size) {
        auto *self=static_cast<TlsStream *>(ctx);
        int n=mux_stream_read(self->token,data,size,20);
        return n>0?n:(n<0?MBEDTLS_ERR_SSL_CONN_EOF:MBEDTLS_ERR_SSL_WANT_READ);
    }
    static int verify_cb(void *ctx,mbedtls_x509_crt *crt,int depth,uint32_t *flags) {
        auto *self=static_cast<TlsStream *>(ctx);
        if (!depth) {
            uint8_t hash[32];
            if (!key_hash(&crt->pk,hash) || memcmp(hash,self->expected_key,32)) return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        }
        // Dedicated USB channel: identity is the exact key obtained from this
        // phone before Pair, not Internet CA/hostname or the board's unset clock.
        *flags=0;
        return 0;
    }
public:
    explicit TlsStream(uint32_t id):Stream(id) {
        mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&config); mbedtls_x509_crt_init(&cert); mbedtls_pk_init(&key);
    }
    ~TlsStream() override {
        mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config); mbedtls_x509_crt_free(&cert); mbedtls_pk_free(&key);
    }
    bool start(const PairRecord &r,const char *name) {
        uint32_t failures_before=alloc_failure_count();
        void *probe=mbedtls_calloc(1,64);
        ESP_LOGI(tag,"TLS_BEGIN %s mbedtls_heap=%s",name,!probe?"exhausted":esp_ptr_external_ram(probe)?"psram":"internal");
        mbedtls_free(probe);
        log_tls_heap(name,"begin");
        memcpy(expected_key,r.phone_key_hash,32);
        int rc=mbedtls_x509_crt_parse(&cert,reinterpret_cast<const uint8_t *>(r.root_cert),strlen(r.root_cert)+1);
        if (!rc) rc=mbedtls_pk_parse_key(&key,reinterpret_cast<const uint8_t *>(r.root_key),strlen(r.root_key)+1,nullptr,0,random_bytes,nullptr);
        if (!rc) rc=mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc) { ESP_LOGE(tag,"TLS configuration failed: -0x%x",-rc); return false; }
        mbedtls_ssl_conf_rng(&config,random_bytes,nullptr);
        mbedtls_ssl_conf_ca_chain(&config,&cert,nullptr);
        mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_verify(&config,verify_cb,this);
        mbedtls_ssl_conf_min_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
        rc=mbedtls_ssl_conf_own_cert(&config,&cert,&key);
        if (!rc) rc=mbedtls_ssl_setup(&ssl,&config);
        // mbedTLS 3.6 requires an explicit peer name with VERIFY_REQUIRED.
        // This USB-only connection still authenticates the pinned phone key in
        // verify_cb; this name does not perform DNS or select another device.
        if (!rc) rc=mbedtls_ssl_set_hostname(&ssl,"Device");
        if (rc) { ESP_LOGE(tag,"TLS setup failed: -0x%x",-rc); return false; }
        mbedtls_ssl_set_bio(&ssl,this,send_cb,recv_cb,nullptr);
        log_tls_heap(name,"setup");
        // Stepped rather than mbedtls_ssl_handshake() so each state is timed and
        // IDLE0 runs between steps; a single RSA operation still cannot be split.
        const int64_t began=esp_timer_get_time(), end=began+20000000;
        int64_t state_us=0, slowest_us=0;
        int state=ssl.MBEDTLS_PRIVATE(state), slowest_state=state;
        while (!mbedtls_ssl_is_handshake_over(&ssl) && esp_timer_get_time()<end) {
            const int64_t step=esp_timer_get_time();
            rc=mbedtls_ssl_handshake_step(&ssl);
            const int64_t spent=esp_timer_get_time()-step;
            state_us+=spent;
            if (spent>slowest_us) { slowest_us=spent; slowest_state=state; }
            if (rc && rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE) break;
            if (ssl.MBEDTLS_PRIVATE(state)!=state) {
                ESP_LOGI(tag,"TLS_STEP %s %s ms=%u dma_free=%u",name,tls_state_name(state),(unsigned)(state_us/1000),
                         (unsigned)heap_caps_get_free_size(dma_caps));
                state=ssl.MBEDTLS_PRIVATE(state); state_us=0;
            }
            vTaskDelay(1);
        }
        const unsigned total_ms=unsigned((esp_timer_get_time()-began)/1000);
        if (!rc && mbedtls_ssl_is_handshake_over(&ssl)) {
            ESP_LOGI(tag,"TLS_READY %s version=%s cipher=%s total_ms=%u slowest=%s/%ums",name,mbedtls_ssl_get_version(&ssl),
                     mbedtls_ssl_get_ciphersuite(&ssl),total_ms,tls_state_name(slowest_state),(unsigned)(slowest_us/1000));
            log_alloc_failures(name,failures_before);
            log_tls_heap(name,"ready");
            return true;
        }
        if (!rc) rc=MBEDTLS_ERR_SSL_TIMEOUT;
        log_alloc_failures(name,failures_before);
        log_tls_heap(name,"failed");
        char detail[160]; mbedtls_strerror(rc,detail,sizeof(detail));
        ESP_LOGE(tag,"TLS handshake failed: %s state=%s total_ms=%u slowest=%s/%ums -0x%x %s",name,tls_state_name(state),total_ms,
                 tls_state_name(slowest_state),(unsigned)(slowest_us/1000),-rc,detail);
        return false;
    }
    int read(uint8_t *out,size_t size,unsigned timeout_ms) override {
        int64_t end=esp_timer_get_time()+int64_t(timeout_ms)*1000;
        do {
            int n=mbedtls_ssl_read(&ssl,out,size);
            if (n>0) return n;
            if (n!=MBEDTLS_ERR_SSL_WANT_READ && n!=MBEDTLS_ERR_SSL_WANT_WRITE) return -1;
            vTaskDelay(1);
        } while (esp_timer_get_time()<end);
        return 0;
    }
    bool write(const uint8_t *data,size_t size) override {
        size_t pos=0; int64_t end=esp_timer_get_time()+10000000;
        while (pos<size && esp_timer_get_time()<end) {
            size_t chunk=size-pos>2048?2048:size-pos;
            int n=mbedtls_ssl_write(&ssl,data+pos,chunk);
            if (n>0) pos+=n;
            else if (n!=MBEDTLS_ERR_SSL_WANT_READ && n!=MBEDTLS_ERR_SSL_WANT_WRITE) return false;
            else vTaskDelay(1);
        }
        return pos==size;
    }
};

bool pair_phone(PairRecord &r,uint32_t epoch) {
    std::string body=std::string(label)+"<key>Request</key><string>Pair</string><key>ProtocolVersion</key><string>2</string>"
        "<key>PairingOptions</key><dict><key>ExtendedPairingErrors</key><true/></dict><key>PairRecord</key><dict>"+
        data_element("DeviceCertificate",r.device_cert)+data_element("HostCertificate",r.host_cert)+
        data_element("RootCertificate",r.root_cert)+"<key>HostID</key><string>"+r.host_id+"</string><key>SystemBUID</key><string>"+r.buid+"</string></dict>";
    int64_t end=esp_timer_get_time()+180000000;
    std::string reported_error;
    while (mux_transport_alive(epoch) && esp_timer_get_time()<end) {
        uint32_t id=mux_stream_open(62078,epoch); if (!id) return false;
        Stream stream(id); std::string reply;
        if (!set_buid(stream,r) || !request(stream,body,reply,15000)) return false;
        std::string error=error_value(reply);
        if (error.empty()) {
            std::string operation;
            if (!text_value(reply,"Request","string",operation) || operation!="Pair") { ESP_LOGE(tag,"Invalid Pair response"); return false; }
            r.trusted=1; if (!save(r)) return false; ESP_LOGI(tag,"PAIR_OK identity saved for this phone"); return true; }
        if (error!="PairingDialogResponsePending" && error!="PasswordProtected") { ESP_LOGE(tag,"Pair rejected: %s",error.c_str()); return false; }
        if (reported_error!=error) {
            ESP_LOGW(tag,"%s: unlock phone and confirm Trust on the phone",error=="PasswordProtected"?"WAITING_FOR_UNLOCK":"TRUST_PENDING");
            reported_error=error;
        }
        mux_stream_close(stream.token); stream.token=0;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGE(tag,"Pairing wait ended"); return false;
}

void session(PairRecord &r,uint32_t epoch) {
    uint32_t id=mux_stream_open(62078,epoch); if (!id) { ESP_LOGE(tag,"Session TCP open failed"); return; }
    auto tls=std::make_unique<TlsStream>(id);
    // TLS transport still starts in plaintext for StartSession.
    Stream plaintext(id); std::string reply;
    bool ok=request(plaintext,std::string(label)+"<key>Request</key><string>StartSession</string><key>HostID</key><string>"+
        r.host_id+"</string><key>SystemBUID</key><string>"+r.buid+"</string>",reply);
    plaintext.token=0;
    if (!ok) return;
    std::string error=error_value(reply);
    if (!error.empty()) {
        ESP_LOGE(tag,"StartSession rejected: %s",error.c_str());
        if (error=="InvalidHostID" || error=="InvalidPairRecord") { r.trusted=0; save(r); }
        return;
    }
    bool ssl=false;
    if (!boolean_value(reply,"EnableSessionSSL",ssl) || !ssl) { ESP_LOGE(tag,"StartSession did not enable TLS"); return; }
    if (!tls->start(r,"lockdown")) return;
    if (!request(*tls,std::string(label)+"<key>Request</key><string>QueryType</string>",reply)) return;
    std::string service_type;
    if (!error_value(reply).empty() || !text_value(reply,"Type","string",service_type) || service_type!="com.apple.mobile.lockdown") {
        ESP_LOGE(tag,"TLS QueryType service verification failed"); return;
    }
    tab5_session_post(epoch,CP_TRUST_OK);
    ESP_LOGI(tag,"LOCKDOWN_TLS_QUERY_OK: encrypted service verified; opening CarKit");
    if (!request(*tls,"<key>Request</key><string>StartService</string><key>Service</key><string>com.apple.carkit.service</string>",reply)) return;
    error=error_value(reply);
    if (!error.empty()) { ESP_LOGE(tag,"StartService rejected: %s",error.c_str()); return; }
    std::string port_text;
    if (!text_value(reply,"Port","integer",port_text) || port_text.empty()) { ESP_LOGE(tag,"Missing CarKit port"); return; }
    uint32_t port=0;
    for (char c:port_text) {
        if (c<'0' || c>'9' || port>6553) { ESP_LOGE(tag,"Invalid CarKit port"); return; }
        port=port*10+unsigned(c-'0');
    }
    if (!port || port>65535) return;
    bool service_ssl=false;
    size_t ssl_pos;
    if (value_position(reply,"EnableServiceSSL",ssl_pos) && !boolean_value(reply,"EnableServiceSSL",service_ssl)) { ESP_LOGE(tag,"Invalid service SSL flag"); return; }
    id=mux_stream_open(uint16_t(port),epoch); if (!id) { ESP_LOGE(tag,"CarKit TCP open failed"); return; }
    std::unique_ptr<Stream> service;
    if (service_ssl) {
        auto encrypted=std::make_unique<TlsStream>(id);
        if (!encrypted->start(r,"carkit")) return;
        service=std::move(encrypted);
    } else service=std::make_unique<Stream>(id);
    ESP_LOGI(tag,"CARKIT_SERVICE_READY encrypted=%s; iAP2 authentication is pending",service_ssl?"yes":"no");
    Iap2Transport transport{
        service.get(),
        [](void *ctx,const uint8_t *p,size_t n) { return static_cast<Stream *>(ctx)->write(p,n); },
        [](void *ctx,uint8_t *p,size_t n,unsigned timeout) { return static_cast<Stream *>(ctx)->read(p,n,timeout); }
    };
    // Keep the owning Lockdown TLS session alive throughout the iAP2 control loop.
    tab5_session_post(epoch,CP_AUTH_BEGIN);
    iap2_client_run(transport,epoch);

}
}

void lockdown_client_run(uint32_t epoch) {
    storage_failed=false;
    heap_caps_register_failed_alloc_callback(note_alloc_failure);
    esp_err_t init=nvs_flash_init();
    if(init!=ESP_OK) { ESP_LOGE(tag,"NVS init failed: %s; no erase performed",esp_err_to_name(init)); tab5_session_post(epoch,CP_STORAGE_ERROR); return; }
    nvs_stats_t stats={};
    if(nvs_get_stats(NULL,&stats)==ESP_OK)
        ESP_LOGI(tag,"NVS entries used=%u free=%u total=%u; pair record bytes=%u",
                 (unsigned)stats.used_entries,(unsigned)stats.free_entries,
                 (unsigned)stats.total_entries,(unsigned)sizeof(PairRecord));
    nvs_handle_t diagnostic;
    if(nvs_open("tab5_pair",NVS_READONLY,&diagnostic)==ESP_OK) {
        nvs_iterator_t it=nullptr; unsigned blobs=0; size_t bytes=0;
        esp_err_t next=nvs_entry_find("nvs","tab5_pair",NVS_TYPE_BLOB,&it);
        while(next==ESP_OK) {
            nvs_entry_info_t info={}; size_t length=0;
            if(nvs_entry_info(it,&info)==ESP_OK &&
               nvs_get_blob(diagnostic,info.key,nullptr,&length)==ESP_OK) { ++blobs; bytes+=length; }
            next=nvs_entry_next(&it);
        }
        nvs_release_iterator(it); nvs_close(diagnostic);
        ESP_LOGI(tag,"Pair storage blobs=%u payload_bytes=%u (no identities logged)",blobs,(unsigned)bytes);
    }

    // This wired-only firmware does not initialize RF or ADC drivers. Keep a
    // physical entropy source active for key generation and TLS, then release it
    // before any subsequent worker or future RF/ADC application can start.
    struct EntropySource {
        EntropySource() { tab5_entropy_acquire(); }
        ~EntropySource() { tab5_entropy_release(); }
    } entropy;
#ifdef TAB5_SOFTWARE_AUTH
    if(auth_software_provider_init()!=ESP_OK) {
        ESP_LOGE(tag,"Software authentication identity validation failed");
        tab5_session_post(epoch,CP_AUTH_MISSING); return;
    }
#endif
    auto *record=static_cast<PairRecord *>(heap_caps_calloc(1,sizeof(PairRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!record) { ESP_LOGE(tag,"Pair record allocation failed"); return; }
    bool loaded=load(*record);
    if (!loaded) { uuid(record->host_id); uuid(record->buid); }
    uint32_t id=mux_stream_open(62078,epoch);
    if (id) {
        std::string public_key;
        {
            Stream initial(id);
            if (!set_buid(initial,*record) || !get_public_key(initial,public_key)) public_key.clear();
        }
        if (!public_key.empty()) {
            Keys keys; uint8_t hash[32];
            int rc=mbedtls_pk_parse_public_key(&keys.device,reinterpret_cast<const uint8_t *>(public_key.c_str()),public_key.size()+1);
            bool have_hash=!rc && key_hash(&keys.device,hash);
            bool same=loaded && record->magic==record_magic && have_hash && !memcmp(hash,record->phone_key_hash,32);
            if (!same && have_hash) {
                mbedtls_platform_zeroize(record,sizeof(*record));
                loaded=load_phone(*record,hash);
                same=loaded && record->magic==record_magic;
            }
            if (same) ESP_LOGI(tag,"PAIR_RECORD_REUSED phone key matched; trusted=%s",record->trusted?"yes":"pending");
            if (same || make_record(*record,public_key)) {
                if ((record->trusted || pair_phone(*record,epoch)) && mux_transport_alive(epoch)) session(*record,epoch);
            }
        }
    } else ESP_LOGE(tag,"Could not open pairing connection");
    mbedtls_platform_zeroize(record,sizeof(*record)); free(record);
    if(storage_failed) tab5_session_post(epoch,CP_STORAGE_ERROR);
    ESP_LOGI(tag,"Lockdown worker ended");
}
