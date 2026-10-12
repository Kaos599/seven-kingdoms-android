/*
 * Seven Kingdoms Android port: LAN multiplayer helpers (see sk_net.h).
 */
#ifdef __ANDROID__

#include <SDL.h>
#include <jni.h>
#include <android/log.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <multiplayer.h>
#include "sk_net.h"

#define LOG_TAG "7kaa"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern MultiPlayer mp_obj;   // AM.cpp

static bool g_lock_held = false;
static char g_last_addr[100] = "";

// ---------------------------------------------------------------------------
// JNI: static methods on SevenKingdomsActivity. The class is taken from the activity object because
// FindClass from a native (SDL) thread would use the system class loader.
// ---------------------------------------------------------------------------

static bool call_activity_void(const char *name)
{
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject act = (jobject)SDL_AndroidGetActivity();
	if (!env || !act)
		return false;
	bool ok = false;
	jclass cls = env->GetObjectClass(act);
	jmethodID m = cls ? env->GetStaticMethodID(cls, name, "()V") : NULL;
	if (m)
	{
		env->CallStaticVoidMethod(cls, m);
		ok = true;
	}
	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		ok = false;
	}
	if (cls)
		env->DeleteLocalRef(cls);
	env->DeleteLocalRef(act);
	return ok;
}

static bool call_activity_string(const char *name, char *out, size_t out_size)
{
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject act = (jobject)SDL_AndroidGetActivity();
	if (!env || !act)
		return false;
	bool ok = false;
	jclass cls = env->GetObjectClass(act);
	jmethodID m = cls ? env->GetStaticMethodID(cls, name, "()Ljava/lang/String;") : NULL;
	if (m)
	{
		jstring js = (jstring)env->CallStaticObjectMethod(cls, m);
		if (!env->ExceptionCheck() && js)
		{
			const char *s = env->GetStringUTFChars(js, NULL);
			if (s)
			{
				snprintf(out, out_size, "%s", s);
				env->ReleaseStringUTFChars(js, s);
				ok = true;
			}
		}
		if (js)
			env->DeleteLocalRef(js);
	}
	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		ok = false;
	}
	if (cls)
		env->DeleteLocalRef(cls);
	env->DeleteLocalRef(act);
	return ok;
}

void sk_net_multicast_acquire(void)
{
	if (g_lock_held)
		return;
	g_lock_held = call_activity_void("acquireMulticastLock");
	LOGI("net: multicast lock %s", g_lock_held ? "acquired" : "NOT acquired (LAN game list may stay empty)");
}

void sk_net_multicast_release(void)
{
	if (!g_lock_held)
		return;
	g_lock_held = false;
	call_activity_void("releaseMulticastLock");
	LOGI("net: multicast lock released");
}

// ---------------------------------------------------------------------------
// Own address
// ---------------------------------------------------------------------------

static void append_addr(char *list, size_t list_size, const char *addr)
{
	if (strstr(list, addr))
		return;
	size_t len = strlen(list);
	if (len + strlen(addr) + 3 >= list_size)
		return;
	snprintf(list + len, list_size - len, "%s%s", len ? ", " : "", addr);
}

const char *sk_net_address_line(void)
{
	static char line[160];
	static Uint32 stamp = 0;
	static bool have = false;

	Uint32 now = SDL_GetTicks();
	if (have && (Sint32)(now - stamp) < 2000)
		return line;

	char list[120];
	list[0] = 0;

	struct ifaddrs *ifa_list = NULL;
	if (getifaddrs(&ifa_list) == 0)
	{
		for (struct ifaddrs *ifa = ifa_list; ifa; ifa = ifa->ifa_next)
		{
			if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
				continue;
			if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
				continue;
			char buf[INET_ADDRSTRLEN];
			const struct sockaddr_in *sin = (const struct sockaddr_in *)ifa->ifa_addr;
			if (!inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)))
				continue;
			append_addr(list, sizeof(list), buf);
		}
		freeifaddrs(ifa_list);
	}

	if (!list[0])   // some Android versions restrict getifaddrs: ask java.net.NetworkInterface instead
		call_activity_string("getLocalIpv4Addresses", list, sizeof(list));

	if (list[0])
		snprintf(line, sizeof(line), "Your address: %s", list);
	else
		snprintf(line, sizeof(line), "Your address: not on a Wi-Fi network");
	stamp = now;
	have = true;
	return line;
}

// ---------------------------------------------------------------------------
// Last joined address, ENet flush
// ---------------------------------------------------------------------------

void sk_net_remember_address(const char *addr)
{
	if (addr && *addr)
		snprintf(g_last_addr, sizeof(g_last_addr), "%s", addr);
}

void sk_net_last_address(char *out, size_t out_size)
{
	snprintf(out, out_size, "%s", g_last_addr);
}

void sk_net_flush(void)
{
	mp_obj.flush();
}

#endif // __ANDROID__
