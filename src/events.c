#include "dshot.h"
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

static int event_write = -1;
static io_service_t root;
static io_connect_t power_port;
static bool lid_events;
static void wake_reader(void) { char b = 'e'; if (event_write >= 0) (void)write(event_write, &b, 1); }
static void power_changed(void *context) { (void)context; wake_reader(); }
static void interest(void *ref, io_service_t service, natural_t type, void *arg) {
    (void)ref; (void)service; (void)type; (void)arg; wake_reader();
}
static void system_power(void *ref, io_service_t service, natural_t type, void *arg) {
    (void)ref; (void)service;
    if (type == kIOMessageCanSystemSleep || type == kIOMessageSystemWillSleep)
        IOAllowPowerChange(power_port, (long)arg);
    if (type == kIOMessageSystemHasPoweredOn) wake_reader();
}
int lid_closed(void) {
#ifdef DSHOT_TEST
    return 0;
#else
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPMrootDomain"));
    if (!service) return -1;
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("AppleClamshellState"), kCFAllocatorDefault, 0);
    IOObjectRelease(service); if (!value) return -1;
    int result = CFGetTypeID(value) == CFBooleanGetTypeID() ? CFBooleanGetValue(value) : -1;
    CFRelease(value); return result;
#endif
}
static void *event_thread(void *unused) {
    (void)unused;
    IONotificationPortRef notification = IONotificationPortCreate(kIOMainPortDefault);
    io_object_t notifier = 0, power_notifier = 0;
    if (lid_events && notification) {
        root = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPMrootDomain"));
        if (root) IOServiceAddInterestNotification(notification, root, kIOGeneralInterest, interest, NULL, &notifier);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notification), kCFRunLoopDefaultMode);
    }
    CFRunLoopSourceRef ps = IOPSNotificationCreateRunLoopSource(power_changed, NULL);
    if (ps) CFRunLoopAddSource(CFRunLoopGetCurrent(), ps, kCFRunLoopDefaultMode);
    IONotificationPortRef power_notification = NULL;
    power_port = IORegisterForSystemPower(NULL, &power_notification, system_power, &power_notifier);
    if (power_port && power_notification)
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(power_notification), kCFRunLoopDefaultMode);
    wake_reader(); CFRunLoopRun();
    return NULL;
}
int events_start(bool lid) {
#ifdef DSHOT_TEST
    (void)lid; return -1;
#else
    int fds[2]; if (pipe(fds)) return -1;
    for (int i = 0; i < 2; i++) { fcntl(fds[i], F_SETFD, FD_CLOEXEC); fcntl(fds[i], F_SETFL, O_NONBLOCK); }
    event_write = fds[1]; lid_events = lid;
    pthread_t thread;
    if (pthread_create(&thread, NULL, event_thread, NULL)) { close(fds[0]); close(fds[1]); return -1; }
    pthread_detach(thread); return fds[0];
#endif
}
static void (*lock_screen)(void);
int lock_available(void) {
#ifdef DSHOT_TEST
    return 1;
#else
    if (!lock_screen) {
        void *bundle = dlopen("/System/Library/PrivateFrameworks/login.framework/Versions/A/login", RTLD_LAZY | RTLD_LOCAL);
        if (bundle) *(void **)(&lock_screen) = dlsym(bundle, "SACLockScreenImmediate");
    }
    return lock_screen != NULL;
#endif
}
static bool screen_locked(void) {
    CFDictionaryRef session = CGSessionCopyCurrentDictionary(); if (!session) return false;
    CFTypeRef value = CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked"));
    bool locked = value && CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(value);
    CFRelease(session); return locked;
}
int lock_and_blank(void) {
#ifdef DSHOT_TEST
    return 0;
#else
    if (!lock_available()) return -1;
    lock_screen();
    /* Lock invocation is asynchronous. Confirm session state before claiming success. */
    double end = now_seconds() + 3;
    while (!screen_locked() && now_seconds() < end) usleep(50000);
    if (!screen_locked()) { diagnostic("could not verify the session locked"); return -1; }
    char *args[] = {"/usr/bin/pmset", "displaysleepnow", NULL};
    return run_bounded(args[0], args, NULL, 0, 3);
#endif
}
