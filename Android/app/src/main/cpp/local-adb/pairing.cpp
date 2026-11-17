#include <jni.h>
#include <openssl/curve25519.h>
#include <openssl/hkdf.h>
#include <openssl/digest.h>
#include <openssl/mem.h>
#include <vector>

// Android's ADB pairing protocol; cryptographic primitives are BoringSSL's.
struct Pairing { SPAKE2_CTX* ctx; std::vector<uint8_t> message; ~Pairing() { SPAKE2_CTX_free(ctx); } };
extern "C" JNIEXPORT jlong JNICALL Java_com_osama_cleanwave_AdbPairingCrypto_create(JNIEnv* env, jclass, jbyteArray password) {
    const uint8_t client[] = "adb pair client", server[] = "adb pair server";
    auto* p = new Pairing{SPAKE2_CTX_new(spake2_role_alice, client, sizeof(client), server, sizeof(server)), {}};
    if (!p->ctx) { delete p; return 0; }
    auto size = env->GetArrayLength(password);
    std::vector<uint8_t> secret(size); env->GetByteArrayRegion(password, 0, size, reinterpret_cast<jbyte*>(secret.data()));
    p->message.resize(SPAKE2_MAX_MSG_SIZE); size_t len = 0;
    int ok = SPAKE2_generate_msg(p->ctx, p->message.data(), &len, p->message.size(), secret.data(), secret.size());
    OPENSSL_cleanse(secret.data(), secret.size());
    if (!ok) { delete p; return 0; } p->message.resize(len); return reinterpret_cast<jlong>(p);
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_osama_cleanwave_AdbPairingCrypto_message(JNIEnv* env, jclass, jlong handle) {
    auto* p = reinterpret_cast<Pairing*>(handle); auto result = env->NewByteArray(p->message.size());
    env->SetByteArrayRegion(result, 0, p->message.size(), reinterpret_cast<jbyte*>(p->message.data())); return result;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_osama_cleanwave_AdbPairingCrypto_finish(JNIEnv* env, jclass, jlong handle, jbyteArray peer) {
    auto* p = reinterpret_cast<Pairing*>(handle); auto size = env->GetArrayLength(peer);
    if (size <= 0 || size > SPAKE2_MAX_MSG_SIZE) return nullptr;
    std::vector<uint8_t> msg(size); env->GetByteArrayRegion(peer, 0, size, reinterpret_cast<jbyte*>(msg.data()));
    uint8_t secret[SPAKE2_MAX_KEY_SIZE], key[16]; size_t len = 0;
    int ok = SPAKE2_process_msg(p->ctx, secret, &len, sizeof(secret), msg.data(), msg.size());
    const uint8_t info[] = "adb pairing_auth aes-128-gcm key";
    if (ok) ok = HKDF(key, sizeof(key), EVP_sha256(), secret, len, nullptr, 0, info, sizeof(info)-1);
    OPENSSL_cleanse(secret, sizeof(secret));
    if (!ok) { OPENSSL_cleanse(key, sizeof(key)); return nullptr; }
    auto result = env->NewByteArray(sizeof(key)); env->SetByteArrayRegion(result, 0, sizeof(key), reinterpret_cast<jbyte*>(key));
    OPENSSL_cleanse(key, sizeof(key)); return result;
}
extern "C" JNIEXPORT void JNICALL Java_com_osama_cleanwave_AdbPairingCrypto_destroy(JNIEnv*, jclass, jlong handle) { delete reinterpret_cast<Pairing*>(handle); }
