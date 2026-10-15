/* NNode ELF module symbol table (custom resolver for elf_loader).
 *
 * Generated from the legacy vendor table by tools/gen_modsym.py.
 * These are the extra firmware symbols (GPIO driver + FreeRTOS)
 * that ELF modules may call, beyond the loader defaults.
 *
 * The `extern int` declarations only take addresses (same trick
 * as upstream esp_all_symbol.c) and pin the functions against
 * linker garbage-collection.
 */
#include <stddef.h>
#include <stdint.h>

/* Local strcmp to avoid string.h (newlib declares __getreent
 * as a function there, clashing with the extern int below). */
static int nnode_streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
/* gpio_ driver group. */
extern int gpio_config;
extern int gpio_config_as_analog;
extern int gpio_deep_sleep_hold_dis;
extern int gpio_deep_sleep_hold_en;
extern int gpio_dump_io_configuration;
extern int gpio_force_hold_all;
extern int gpio_force_unhold_all;
extern int gpio_func_sel;
extern int gpio_get_drive_capability;
extern int gpio_get_io_config;
extern int gpio_get_level;
extern int gpio_hold_dis;
extern int gpio_hold_en;
extern int gpio_input_enable;
extern int gpio_install_isr_service;
extern int gpio_intr_disable;
extern int gpio_intr_enable;
extern int gpio_iomux_input;
extern int gpio_iomux_output;
extern int gpio_isr_handler_add;
extern int gpio_isr_handler_remove;
extern int gpio_isr_register;
extern int gpio_od_disable;
extern int gpio_od_enable;
extern int gpio_output_disable;
extern int gpio_output_enable;
extern int gpio_pulldown_dis;
extern int gpio_pulldown_en;
extern int gpio_pullup_dis;
extern int gpio_pullup_en;
extern int gpio_reset_pin;
extern int gpio_set_direction;
extern int gpio_set_drive_capability;
extern int gpio_set_intr_type;
extern int gpio_set_level;
extern int gpio_set_pull_mode;
extern int gpio_sleep_sel_dis;
extern int gpio_sleep_sel_en;
extern int gpio_sleep_set_direction;
extern int gpio_sleep_set_pull_mode;
extern int gpio_uninstall_isr_service;
extern int gpio_wakeup_disable;
extern int gpio_wakeup_enable;
extern int gpio_wakeup_disable_on_hp_periph_powerdown_sleep;
extern int gpio_wakeup_enable_on_hp_periph_powerdown_sleep;
extern int gpio_del_glitch_filter;
extern int gpio_glitch_filter_disable;
extern int gpio_glitch_filter_enable;
extern int gpio_new_pin_glitch_filter;
/* rtc_gpio_ group. */
extern int rtc_gpio_is_valid_gpio;
/* dedic_gpio_ (dedicated GPIO) group. */
extern int dedic_gpio_bundle_read_in;
extern int dedic_gpio_bundle_read_out;
extern int dedic_gpio_bundle_write;
extern int dedic_gpio_del_bundle;
extern int dedic_gpio_get_in_mask;
extern int dedic_gpio_get_in_offset;
extern int dedic_gpio_get_out_mask;
extern int dedic_gpio_get_out_offset;
extern int dedic_gpio_new_bundle;
/* FreeRTOS kernel group. */
extern int pvPortMalloc;
extern int vPortFree;
extern int xPortCheckValidListMem;
extern int xPortcheckValidStackMem;
extern int xPortCheckValidTCBMem;
extern int xPortGetFreeHeapSize;
extern int xPortGetMinimumEverFreeHeapSize;
extern int esp_startup_start_app;
extern int vApplicationGetIdleTaskMemory;
extern int vApplicationGetTimerTaskMemory;
extern int SysTickIsrHandler;
extern int vPortSetupTimer;
extern int vSystimerSetup;
extern int xPortSysTickHandler;
extern int uxListRemove;
extern int vListInitialise;
extern int vListInitialiseItem;
extern int vListInsert;
extern int vListInsertEnd;
extern int ucQueueGetQueueType;
extern int uxQueueGetQueueNumber;
extern int uxQueueMessagesWaiting;
extern int uxQueueMessagesWaitingFromISR;
extern int uxQueueSpacesAvailable;
extern int vQueueDelete;
extern int vQueueSetQueueNumber;
extern int vQueueWaitForMessageRestricted;
extern int xQueueAddToSet;
extern int xQueueCreateCountingSemaphore;
extern int xQueueCreateCountingSemaphoreStatic;
extern int xQueueCreateMutex;
extern int xQueueCreateMutexStatic;
extern int xQueueCreateSet;
extern int xQueueGenericCreate;
extern int xQueueGenericCreateStatic;
extern int xQueueGenericGetStaticBuffers;
extern int xQueueGenericReset;
extern int xQueueGenericSend;
extern int xQueueGenericSendFromISR;
extern int xQueueGetMutexHolder;
extern int xQueueGetMutexHolderFromISR;
extern int xQueueGiveFromISR;
extern int xQueueGiveMutexRecursive;
extern int xQueueIsQueueEmptyFromISR;
extern int xQueueIsQueueFullFromISR;
extern int xQueuePeek;
extern int xQueuePeekFromISR;
extern int xQueueReceive;
extern int xQueueReceiveFromISR;
extern int xQueueRemoveFromSet;
extern int xQueueSelectFromSet;
extern int xQueueSelectFromSetFromISR;
extern int xQueueSemaphoreTake;
extern int xQueueTakeMutexRecursive;
extern int eTaskGetState;
extern int __getreent;
extern int pcTaskGetName;
extern int prvTaskPriorityRaise;
extern int prvTaskPriorityRestore;
extern int pvTaskGetCurrentTCBForCore;
extern int pvTaskGetThreadLocalStoragePointer;
extern int pvTaskIncrementMutexHeldCount;
extern int pxCurrentTCBs;
extern int pxTaskGetStackStart;
extern int ulTaskGenericNotifyTake;
extern int ulTaskGenericNotifyValueClear;
extern int ulTaskGetIdleRunTimeCounter;
extern int ulTaskGetIdleRunTimeCounterForCore;
extern int ulTaskGetIdleRunTimePercent;
extern int ulTaskGetIdleRunTimePercentForCore;
extern int uxTaskGetNumberOfTasks;
extern int uxTaskGetSnapshotAll;
extern int uxTaskGetStackHighWaterMark;
extern int uxTaskGetStackHighWaterMark2;
extern int uxTaskGetSystemState;
extern int uxTaskGetTaskNumber;
extern int uxTaskPriorityGet;
extern int uxTaskPriorityGetFromISR;
extern int uxTaskResetEventItemValue;
extern int vTaskDelay;
extern int vTaskDelete;
extern int vTaskEndScheduler;
extern int vTaskGenericNotifyGiveFromISR;
extern int vTaskGetInfo;
extern int vTaskGetRunTimeStats;
extern int vTaskGetSnapshot;
extern int vTaskInternalSetTimeOutState;
extern int vTaskList;
extern int vTaskMissedYield;
extern int vTaskPlaceOnEventList;
extern int vTaskPlaceOnEventListRestricted;
extern int vTaskPlaceOnUnorderedEventList;
extern int vTaskPriorityDisinheritAfterTimeout;
extern int vTaskPrioritySet;
extern int vTaskRemoveFromUnorderedEventList;
extern int vTaskResume;
extern int vTaskSetTaskNumber;
extern int vTaskSetThreadLocalStoragePointer;
extern int vTaskSetThreadLocalStoragePointerAndDelCallback;
extern int vTaskSetTimeOutState;
extern int vTaskStartScheduler;
extern int vTaskSuspend;
extern int vTaskSuspendAll;
extern int vTaskSwitchContext;
extern int xTaskAbortDelay;
extern int xTaskCatchUpTicks;
extern int xTaskCheckForTimeOut;
extern int xTaskCreatePinnedToCore;
extern int xTaskCreateStaticPinnedToCore;
extern int xTaskDelayUntil;
extern int xTaskGenericNotify;
extern int xTaskGenericNotifyFromISR;
extern int xTaskGenericNotifyStateClear;
extern int xTaskGenericNotifyWait;
extern int xTaskGetCoreID;
extern int xTaskGetCurrentTaskHandle;
extern int xTaskGetCurrentTaskHandleForCore;
extern int xTaskGetHandle;
extern int xTaskGetIdleTaskHandle;
extern int xTaskGetIdleTaskHandleForCore;
extern int xTaskGetNext;
extern int xTaskGetSchedulerState;
extern int xTaskGetStaticBuffers;
extern int xTaskGetTickCount;
extern int xTaskGetTickCountFromISR;
extern int xTaskIncrementTick;
extern int xTaskPriorityDisinherit;
extern int xTaskPriorityInherit;
extern int xTaskRemoveFromEventList;
extern int xTaskResumeAll;
extern int xTaskResumeFromISR;
extern int pcTimerGetName;
extern int pvTimerGetTimerID;
extern int uxTimerGetReloadMode;
extern int uxTimerGetTimerNumber;
extern int vTimerSetReloadMode;
extern int vTimerSetTimerID;
extern int vTimerSetTimerNumber;
extern int xTimerCreate;
extern int xTimerCreateStatic;
extern int xTimerCreateTimerTask;
extern int xTimerGenericCommand;
extern int xTimerGetExpiryTime;
extern int xTimerGetPeriod;
extern int xTimerGetReloadMode;
extern int xTimerGetStaticBuffer;
extern int xTimerGetTimerDaemonTaskHandle;
extern int xTimerIsTimerActive;
extern int xTimerPendFunctionCall;
extern int xTimerPendFunctionCallFromISR;
extern int uxEventGroupGetNumber;
extern int vEventGroupClearBitsCallback;
extern int vEventGroupDelete;
extern int vEventGroupSetBitsCallback;
extern int vEventGroupSetNumber;
extern int xEventGroupClearBits;
extern int xEventGroupClearBitsFromISR;
extern int xEventGroupCreate;
extern int xEventGroupCreateStatic;
extern int xEventGroupGetBitsFromISR;
extern int xEventGroupGetStaticBuffer;
extern int xEventGroupSetBits;
extern int xEventGroupSetBitsFromISR;
extern int xEventGroupSync;
extern int xEventGroupWaitBits;
extern int ucStreamBufferGetStreamBufferType;
extern int uxStreamBufferGetStreamBufferNumber;
extern int vStreamBufferDelete;
extern int vStreamBufferSetStreamBufferNumber;
extern int xStreamBufferBytesAvailable;
extern int xStreamBufferGenericCreate;
extern int xStreamBufferGenericCreateStatic;
extern int xStreamBufferGetStaticBuffers;
extern int xStreamBufferIsEmpty;
extern int xStreamBufferIsFull;
extern int xStreamBufferNextMessageLengthBytes;
extern int xStreamBufferReceive;
extern int xStreamBufferReceiveCompletedFromISR;
extern int xStreamBufferReceiveFromISR;
extern int xStreamBufferReset;
extern int xStreamBufferSend;
extern int xStreamBufferSendCompletedFromISR;
extern int xStreamBufferSendFromISR;
extern int xStreamBufferSetTriggerLevel;
extern int xStreamBufferSpacesAvailable;
extern int port_uxCoreStartupDone;
extern int port_uxCriticalNesting;
extern int port_uxInterruptNesting;
extern int port_uxOldInterruptState;
extern int port_xSchedulerRunning;
extern int pxPortInitialiseStack;
extern int vPortAssertIfInISR;
extern int vPortClearInterruptMaskFromISR;
extern int vPortEndScheduler;
extern int vPortEnterCritical;
extern int vPortExitCritical;
extern int vPortSetStackWatchpoint;
extern int vPortTCBPreDeleteHook;
extern int vPortYield;
extern int vPortYieldFromISR;
extern int vPortYieldOtherCore;
extern int xIsrStack;
extern int xIsrStackBottom;
extern int xIsrStackTop;
extern int xPortGetTickRateHz;
extern int xPortInIsrContext;
extern int xPortInterruptedFromISRContext;
extern int xPortSetInterruptMaskFromISR;
extern int xPortStartScheduler;
extern int xPortSwitchFlag;
extern int rtos_int_enter;
extern int rtos_int_exit;
extern int vEventGroupDeleteWithCaps;
extern int xEventGroupCreateWithCaps;
extern int vQueueDeleteWithCaps;
extern int vSemaphoreDeleteWithCaps;
extern int vStreamBufferGenericDeleteWithCaps;
extern int vTaskDeleteWithCaps;
extern int xQueueCreateWithCaps;
extern int xSemaphoreCreateGenericWithCaps;
extern int xStreamBufferGenericCreateWithCaps;
extern int xTaskCreatePinnedToCoreWithCaps;
#pragma GCC diagnostic pop

typedef struct { const char *name; uintptr_t addr; } nnode_sym_t;

static const nnode_sym_t kNNodeSyms[] = {
    { "gpio_config", (uintptr_t)&gpio_config },
    { "gpio_config_as_analog", (uintptr_t)&gpio_config_as_analog },
    { "gpio_deep_sleep_hold_dis", (uintptr_t)&gpio_deep_sleep_hold_dis },
    { "gpio_deep_sleep_hold_en", (uintptr_t)&gpio_deep_sleep_hold_en },
    { "gpio_dump_io_configuration", (uintptr_t)&gpio_dump_io_configuration },
    { "gpio_force_hold_all", (uintptr_t)&gpio_force_hold_all },
    { "gpio_force_unhold_all", (uintptr_t)&gpio_force_unhold_all },
    { "gpio_func_sel", (uintptr_t)&gpio_func_sel },
    { "gpio_get_drive_capability", (uintptr_t)&gpio_get_drive_capability },
    { "gpio_get_io_config", (uintptr_t)&gpio_get_io_config },
    { "gpio_get_level", (uintptr_t)&gpio_get_level },
    { "gpio_hold_dis", (uintptr_t)&gpio_hold_dis },
    { "gpio_hold_en", (uintptr_t)&gpio_hold_en },
    { "gpio_input_enable", (uintptr_t)&gpio_input_enable },
    { "gpio_install_isr_service", (uintptr_t)&gpio_install_isr_service },
    { "gpio_intr_disable", (uintptr_t)&gpio_intr_disable },
    { "gpio_intr_enable", (uintptr_t)&gpio_intr_enable },
    { "gpio_iomux_input", (uintptr_t)&gpio_iomux_input },
    { "gpio_iomux_output", (uintptr_t)&gpio_iomux_output },
    { "gpio_isr_handler_add", (uintptr_t)&gpio_isr_handler_add },
    { "gpio_isr_handler_remove", (uintptr_t)&gpio_isr_handler_remove },
    { "gpio_isr_register", (uintptr_t)&gpio_isr_register },
    { "gpio_od_disable", (uintptr_t)&gpio_od_disable },
    { "gpio_od_enable", (uintptr_t)&gpio_od_enable },
    { "gpio_output_disable", (uintptr_t)&gpio_output_disable },
    { "gpio_output_enable", (uintptr_t)&gpio_output_enable },
    { "gpio_pulldown_dis", (uintptr_t)&gpio_pulldown_dis },
    { "gpio_pulldown_en", (uintptr_t)&gpio_pulldown_en },
    { "gpio_pullup_dis", (uintptr_t)&gpio_pullup_dis },
    { "gpio_pullup_en", (uintptr_t)&gpio_pullup_en },
    { "gpio_reset_pin", (uintptr_t)&gpio_reset_pin },
    { "gpio_set_direction", (uintptr_t)&gpio_set_direction },
    { "gpio_set_drive_capability", (uintptr_t)&gpio_set_drive_capability },
    { "gpio_set_intr_type", (uintptr_t)&gpio_set_intr_type },
    { "gpio_set_level", (uintptr_t)&gpio_set_level },
    { "gpio_set_pull_mode", (uintptr_t)&gpio_set_pull_mode },
    { "gpio_sleep_sel_dis", (uintptr_t)&gpio_sleep_sel_dis },
    { "gpio_sleep_sel_en", (uintptr_t)&gpio_sleep_sel_en },
    { "gpio_sleep_set_direction", (uintptr_t)&gpio_sleep_set_direction },
    { "gpio_sleep_set_pull_mode", (uintptr_t)&gpio_sleep_set_pull_mode },
    { "gpio_uninstall_isr_service", (uintptr_t)&gpio_uninstall_isr_service },
    { "gpio_wakeup_disable", (uintptr_t)&gpio_wakeup_disable },
    { "gpio_wakeup_enable", (uintptr_t)&gpio_wakeup_enable },
    { "gpio_wakeup_disable_on_hp_periph_powerdown_sleep", (uintptr_t)&gpio_wakeup_disable_on_hp_periph_powerdown_sleep },
    { "gpio_wakeup_enable_on_hp_periph_powerdown_sleep", (uintptr_t)&gpio_wakeup_enable_on_hp_periph_powerdown_sleep },
    { "gpio_del_glitch_filter", (uintptr_t)&gpio_del_glitch_filter },
    { "gpio_glitch_filter_disable", (uintptr_t)&gpio_glitch_filter_disable },
    { "gpio_glitch_filter_enable", (uintptr_t)&gpio_glitch_filter_enable },
    { "gpio_new_pin_glitch_filter", (uintptr_t)&gpio_new_pin_glitch_filter },
    /* --- end of gpio_ driver group (49 exported).
     * ESP-IDF v6.0: gpio_deep_sleep_wakeup_enable/disable were removed,
     * replaced by gpio_wakeup_enable/disable_on_hp_periph_powerdown_sleep;
     * gpio_iomux_in/out were removed, use gpio_iomux_input/output.
     * Deliberately not exported (HAL internals, not part of the driver API normal modules use):
     * gpio_hal_intr_disable, gpio_hal_intr_enable_on_core, gpio_hal_iomux_in, gpio_hal_iomux_out, gpio_hal_isolate_in_sleep. */
    { "rtc_gpio_is_valid_gpio", (uintptr_t)&rtc_gpio_is_valid_gpio },
    /* --- end of rtc_gpio_ group: complete, all 1 exported. */
    { "dedic_gpio_bundle_read_in", (uintptr_t)&dedic_gpio_bundle_read_in },
    { "dedic_gpio_bundle_read_out", (uintptr_t)&dedic_gpio_bundle_read_out },
    { "dedic_gpio_bundle_write", (uintptr_t)&dedic_gpio_bundle_write },
    { "dedic_gpio_del_bundle", (uintptr_t)&dedic_gpio_del_bundle },
    { "dedic_gpio_get_in_mask", (uintptr_t)&dedic_gpio_get_in_mask },
    { "dedic_gpio_get_in_offset", (uintptr_t)&dedic_gpio_get_in_offset },
    { "dedic_gpio_get_out_mask", (uintptr_t)&dedic_gpio_get_out_mask },
    { "dedic_gpio_get_out_offset", (uintptr_t)&dedic_gpio_get_out_offset },
    { "dedic_gpio_new_bundle", (uintptr_t)&dedic_gpio_new_bundle },
    /* --- end of dedic_gpio_ (dedicated GPIO) group: complete, all 9 exported. */
    { "pvPortMalloc", (uintptr_t)&pvPortMalloc },
    { "vPortFree", (uintptr_t)&vPortFree },
    { "xPortCheckValidListMem", (uintptr_t)&xPortCheckValidListMem },
    { "xPortcheckValidStackMem", (uintptr_t)&xPortcheckValidStackMem },
    { "xPortCheckValidTCBMem", (uintptr_t)&xPortCheckValidTCBMem },
    { "xPortGetFreeHeapSize", (uintptr_t)&xPortGetFreeHeapSize },
    { "xPortGetMinimumEverFreeHeapSize", (uintptr_t)&xPortGetMinimumEverFreeHeapSize },
    { "esp_startup_start_app", (uintptr_t)&esp_startup_start_app },
    { "vApplicationGetIdleTaskMemory", (uintptr_t)&vApplicationGetIdleTaskMemory },
    { "vApplicationGetTimerTaskMemory", (uintptr_t)&vApplicationGetTimerTaskMemory },
    { "SysTickIsrHandler", (uintptr_t)&SysTickIsrHandler },
    { "vPortSetupTimer", (uintptr_t)&vPortSetupTimer },
    { "vSystimerSetup", (uintptr_t)&vSystimerSetup },
    { "xPortSysTickHandler", (uintptr_t)&xPortSysTickHandler },
    { "uxListRemove", (uintptr_t)&uxListRemove },
    { "vListInitialise", (uintptr_t)&vListInitialise },
    { "vListInitialiseItem", (uintptr_t)&vListInitialiseItem },
    { "vListInsert", (uintptr_t)&vListInsert },
    { "vListInsertEnd", (uintptr_t)&vListInsertEnd },
    { "ucQueueGetQueueType", (uintptr_t)&ucQueueGetQueueType },
    { "uxQueueGetQueueNumber", (uintptr_t)&uxQueueGetQueueNumber },
    { "uxQueueMessagesWaiting", (uintptr_t)&uxQueueMessagesWaiting },
    { "uxQueueMessagesWaitingFromISR", (uintptr_t)&uxQueueMessagesWaitingFromISR },
    { "uxQueueSpacesAvailable", (uintptr_t)&uxQueueSpacesAvailable },
    { "vQueueDelete", (uintptr_t)&vQueueDelete },
    { "vQueueSetQueueNumber", (uintptr_t)&vQueueSetQueueNumber },
    { "vQueueWaitForMessageRestricted", (uintptr_t)&vQueueWaitForMessageRestricted },
    { "xQueueAddToSet", (uintptr_t)&xQueueAddToSet },
    { "xQueueCreateCountingSemaphore", (uintptr_t)&xQueueCreateCountingSemaphore },
    { "xQueueCreateCountingSemaphoreStatic", (uintptr_t)&xQueueCreateCountingSemaphoreStatic },
    { "xQueueCreateMutex", (uintptr_t)&xQueueCreateMutex },
    { "xQueueCreateMutexStatic", (uintptr_t)&xQueueCreateMutexStatic },
    { "xQueueCreateSet", (uintptr_t)&xQueueCreateSet },
    { "xQueueGenericCreate", (uintptr_t)&xQueueGenericCreate },
    { "xQueueGenericCreateStatic", (uintptr_t)&xQueueGenericCreateStatic },
    { "xQueueGenericGetStaticBuffers", (uintptr_t)&xQueueGenericGetStaticBuffers },
    { "xQueueGenericReset", (uintptr_t)&xQueueGenericReset },
    { "xQueueGenericSend", (uintptr_t)&xQueueGenericSend },
    { "xQueueGenericSendFromISR", (uintptr_t)&xQueueGenericSendFromISR },
    { "xQueueGetMutexHolder", (uintptr_t)&xQueueGetMutexHolder },
    { "xQueueGetMutexHolderFromISR", (uintptr_t)&xQueueGetMutexHolderFromISR },
    { "xQueueGiveFromISR", (uintptr_t)&xQueueGiveFromISR },
    { "xQueueGiveMutexRecursive", (uintptr_t)&xQueueGiveMutexRecursive },
    { "xQueueIsQueueEmptyFromISR", (uintptr_t)&xQueueIsQueueEmptyFromISR },
    { "xQueueIsQueueFullFromISR", (uintptr_t)&xQueueIsQueueFullFromISR },
    { "xQueuePeek", (uintptr_t)&xQueuePeek },
    { "xQueuePeekFromISR", (uintptr_t)&xQueuePeekFromISR },
    { "xQueueReceive", (uintptr_t)&xQueueReceive },
    { "xQueueReceiveFromISR", (uintptr_t)&xQueueReceiveFromISR },
    { "xQueueRemoveFromSet", (uintptr_t)&xQueueRemoveFromSet },
    { "xQueueSelectFromSet", (uintptr_t)&xQueueSelectFromSet },
    { "xQueueSelectFromSetFromISR", (uintptr_t)&xQueueSelectFromSetFromISR },
    { "xQueueSemaphoreTake", (uintptr_t)&xQueueSemaphoreTake },
    { "xQueueTakeMutexRecursive", (uintptr_t)&xQueueTakeMutexRecursive },
    { "eTaskGetState", (uintptr_t)&eTaskGetState },
    { "__getreent", (uintptr_t)&__getreent },
    { "pcTaskGetName", (uintptr_t)&pcTaskGetName },
    { "prvTaskPriorityRaise", (uintptr_t)&prvTaskPriorityRaise },
    { "prvTaskPriorityRestore", (uintptr_t)&prvTaskPriorityRestore },
    { "pvTaskGetCurrentTCBForCore", (uintptr_t)&pvTaskGetCurrentTCBForCore },
    { "pvTaskGetThreadLocalStoragePointer", (uintptr_t)&pvTaskGetThreadLocalStoragePointer },
    { "pvTaskIncrementMutexHeldCount", (uintptr_t)&pvTaskIncrementMutexHeldCount },
    { "pxCurrentTCBs", (uintptr_t)&pxCurrentTCBs },
    { "pxTaskGetStackStart", (uintptr_t)&pxTaskGetStackStart },
    { "ulTaskGenericNotifyTake", (uintptr_t)&ulTaskGenericNotifyTake },
    { "ulTaskGenericNotifyValueClear", (uintptr_t)&ulTaskGenericNotifyValueClear },
    { "ulTaskGetIdleRunTimeCounter", (uintptr_t)&ulTaskGetIdleRunTimeCounter },
    { "ulTaskGetIdleRunTimeCounterForCore", (uintptr_t)&ulTaskGetIdleRunTimeCounterForCore },
    { "ulTaskGetIdleRunTimePercent", (uintptr_t)&ulTaskGetIdleRunTimePercent },
    { "ulTaskGetIdleRunTimePercentForCore", (uintptr_t)&ulTaskGetIdleRunTimePercentForCore },
    { "uxTaskGetNumberOfTasks", (uintptr_t)&uxTaskGetNumberOfTasks },
    { "uxTaskGetSnapshotAll", (uintptr_t)&uxTaskGetSnapshotAll },
    { "uxTaskGetStackHighWaterMark", (uintptr_t)&uxTaskGetStackHighWaterMark },
    { "uxTaskGetStackHighWaterMark2", (uintptr_t)&uxTaskGetStackHighWaterMark2 },
    { "uxTaskGetSystemState", (uintptr_t)&uxTaskGetSystemState },
    { "uxTaskGetTaskNumber", (uintptr_t)&uxTaskGetTaskNumber },
    { "uxTaskPriorityGet", (uintptr_t)&uxTaskPriorityGet },
    { "uxTaskPriorityGetFromISR", (uintptr_t)&uxTaskPriorityGetFromISR },
    { "uxTaskResetEventItemValue", (uintptr_t)&uxTaskResetEventItemValue },
    { "vTaskDelay", (uintptr_t)&vTaskDelay },
    { "vTaskDelete", (uintptr_t)&vTaskDelete },
    { "vTaskEndScheduler", (uintptr_t)&vTaskEndScheduler },
    { "vTaskGenericNotifyGiveFromISR", (uintptr_t)&vTaskGenericNotifyGiveFromISR },
    { "vTaskGetInfo", (uintptr_t)&vTaskGetInfo },
    { "vTaskGetRunTimeStats", (uintptr_t)&vTaskGetRunTimeStats },
    { "vTaskGetSnapshot", (uintptr_t)&vTaskGetSnapshot },
    { "vTaskInternalSetTimeOutState", (uintptr_t)&vTaskInternalSetTimeOutState },
    { "vTaskList", (uintptr_t)&vTaskList },
    { "vTaskMissedYield", (uintptr_t)&vTaskMissedYield },
    { "vTaskPlaceOnEventList", (uintptr_t)&vTaskPlaceOnEventList },
    { "vTaskPlaceOnEventListRestricted", (uintptr_t)&vTaskPlaceOnEventListRestricted },
    { "vTaskPlaceOnUnorderedEventList", (uintptr_t)&vTaskPlaceOnUnorderedEventList },
    { "vTaskPriorityDisinheritAfterTimeout", (uintptr_t)&vTaskPriorityDisinheritAfterTimeout },
    { "vTaskPrioritySet", (uintptr_t)&vTaskPrioritySet },
    { "vTaskRemoveFromUnorderedEventList", (uintptr_t)&vTaskRemoveFromUnorderedEventList },
    { "vTaskResume", (uintptr_t)&vTaskResume },
    { "vTaskSetTaskNumber", (uintptr_t)&vTaskSetTaskNumber },
    { "vTaskSetThreadLocalStoragePointer", (uintptr_t)&vTaskSetThreadLocalStoragePointer },
    { "vTaskSetThreadLocalStoragePointerAndDelCallback", (uintptr_t)&vTaskSetThreadLocalStoragePointerAndDelCallback },
    { "vTaskSetTimeOutState", (uintptr_t)&vTaskSetTimeOutState },
    { "vTaskStartScheduler", (uintptr_t)&vTaskStartScheduler },
    { "vTaskSuspend", (uintptr_t)&vTaskSuspend },
    { "vTaskSuspendAll", (uintptr_t)&vTaskSuspendAll },
    { "vTaskSwitchContext", (uintptr_t)&vTaskSwitchContext },
    { "xTaskAbortDelay", (uintptr_t)&xTaskAbortDelay },
    { "xTaskCatchUpTicks", (uintptr_t)&xTaskCatchUpTicks },
    { "xTaskCheckForTimeOut", (uintptr_t)&xTaskCheckForTimeOut },
    { "xTaskCreatePinnedToCore", (uintptr_t)&xTaskCreatePinnedToCore },
    { "xTaskCreateStaticPinnedToCore", (uintptr_t)&xTaskCreateStaticPinnedToCore },
    { "xTaskDelayUntil", (uintptr_t)&xTaskDelayUntil },
    { "xTaskGenericNotify", (uintptr_t)&xTaskGenericNotify },
    { "xTaskGenericNotifyFromISR", (uintptr_t)&xTaskGenericNotifyFromISR },
    { "xTaskGenericNotifyStateClear", (uintptr_t)&xTaskGenericNotifyStateClear },
    { "xTaskGenericNotifyWait", (uintptr_t)&xTaskGenericNotifyWait },
    { "xTaskGetCoreID", (uintptr_t)&xTaskGetCoreID },
    { "xTaskGetCurrentTaskHandle", (uintptr_t)&xTaskGetCurrentTaskHandle },
    { "xTaskGetCurrentTaskHandleForCore", (uintptr_t)&xTaskGetCurrentTaskHandleForCore },
    { "xTaskGetHandle", (uintptr_t)&xTaskGetHandle },
    { "xTaskGetIdleTaskHandle", (uintptr_t)&xTaskGetIdleTaskHandle },
    { "xTaskGetIdleTaskHandleForCore", (uintptr_t)&xTaskGetIdleTaskHandleForCore },
    { "xTaskGetNext", (uintptr_t)&xTaskGetNext },
    { "xTaskGetSchedulerState", (uintptr_t)&xTaskGetSchedulerState },
    { "xTaskGetStaticBuffers", (uintptr_t)&xTaskGetStaticBuffers },
    { "xTaskGetTickCount", (uintptr_t)&xTaskGetTickCount },
    { "xTaskGetTickCountFromISR", (uintptr_t)&xTaskGetTickCountFromISR },
    { "xTaskIncrementTick", (uintptr_t)&xTaskIncrementTick },
    { "xTaskPriorityDisinherit", (uintptr_t)&xTaskPriorityDisinherit },
    { "xTaskPriorityInherit", (uintptr_t)&xTaskPriorityInherit },
    { "xTaskRemoveFromEventList", (uintptr_t)&xTaskRemoveFromEventList },
    { "xTaskResumeAll", (uintptr_t)&xTaskResumeAll },
    { "xTaskResumeFromISR", (uintptr_t)&xTaskResumeFromISR },
    { "pcTimerGetName", (uintptr_t)&pcTimerGetName },
    { "pvTimerGetTimerID", (uintptr_t)&pvTimerGetTimerID },
    { "uxTimerGetReloadMode", (uintptr_t)&uxTimerGetReloadMode },
    { "uxTimerGetTimerNumber", (uintptr_t)&uxTimerGetTimerNumber },
    { "vTimerSetReloadMode", (uintptr_t)&vTimerSetReloadMode },
    { "vTimerSetTimerID", (uintptr_t)&vTimerSetTimerID },
    { "vTimerSetTimerNumber", (uintptr_t)&vTimerSetTimerNumber },
    { "xTimerCreate", (uintptr_t)&xTimerCreate },
    { "xTimerCreateStatic", (uintptr_t)&xTimerCreateStatic },
    { "xTimerCreateTimerTask", (uintptr_t)&xTimerCreateTimerTask },
    { "xTimerGenericCommand", (uintptr_t)&xTimerGenericCommand },
    { "xTimerGetExpiryTime", (uintptr_t)&xTimerGetExpiryTime },
    { "xTimerGetPeriod", (uintptr_t)&xTimerGetPeriod },
    { "xTimerGetReloadMode", (uintptr_t)&xTimerGetReloadMode },
    { "xTimerGetStaticBuffer", (uintptr_t)&xTimerGetStaticBuffer },
    { "xTimerGetTimerDaemonTaskHandle", (uintptr_t)&xTimerGetTimerDaemonTaskHandle },
    { "xTimerIsTimerActive", (uintptr_t)&xTimerIsTimerActive },
    { "xTimerPendFunctionCall", (uintptr_t)&xTimerPendFunctionCall },
    { "xTimerPendFunctionCallFromISR", (uintptr_t)&xTimerPendFunctionCallFromISR },
    { "uxEventGroupGetNumber", (uintptr_t)&uxEventGroupGetNumber },
    { "vEventGroupClearBitsCallback", (uintptr_t)&vEventGroupClearBitsCallback },
    { "vEventGroupDelete", (uintptr_t)&vEventGroupDelete },
    { "vEventGroupSetBitsCallback", (uintptr_t)&vEventGroupSetBitsCallback },
    { "vEventGroupSetNumber", (uintptr_t)&vEventGroupSetNumber },
    { "xEventGroupClearBits", (uintptr_t)&xEventGroupClearBits },
    { "xEventGroupClearBitsFromISR", (uintptr_t)&xEventGroupClearBitsFromISR },
    { "xEventGroupCreate", (uintptr_t)&xEventGroupCreate },
    { "xEventGroupCreateStatic", (uintptr_t)&xEventGroupCreateStatic },
    { "xEventGroupGetBitsFromISR", (uintptr_t)&xEventGroupGetBitsFromISR },
    { "xEventGroupGetStaticBuffer", (uintptr_t)&xEventGroupGetStaticBuffer },
    { "xEventGroupSetBits", (uintptr_t)&xEventGroupSetBits },
    { "xEventGroupSetBitsFromISR", (uintptr_t)&xEventGroupSetBitsFromISR },
    { "xEventGroupSync", (uintptr_t)&xEventGroupSync },
    { "xEventGroupWaitBits", (uintptr_t)&xEventGroupWaitBits },
    { "ucStreamBufferGetStreamBufferType", (uintptr_t)&ucStreamBufferGetStreamBufferType },
    { "uxStreamBufferGetStreamBufferNumber", (uintptr_t)&uxStreamBufferGetStreamBufferNumber },
    { "vStreamBufferDelete", (uintptr_t)&vStreamBufferDelete },
    { "vStreamBufferSetStreamBufferNumber", (uintptr_t)&vStreamBufferSetStreamBufferNumber },
    { "xStreamBufferBytesAvailable", (uintptr_t)&xStreamBufferBytesAvailable },
    { "xStreamBufferGenericCreate", (uintptr_t)&xStreamBufferGenericCreate },
    { "xStreamBufferGenericCreateStatic", (uintptr_t)&xStreamBufferGenericCreateStatic },
    { "xStreamBufferGetStaticBuffers", (uintptr_t)&xStreamBufferGetStaticBuffers },
    { "xStreamBufferIsEmpty", (uintptr_t)&xStreamBufferIsEmpty },
    { "xStreamBufferIsFull", (uintptr_t)&xStreamBufferIsFull },
    { "xStreamBufferNextMessageLengthBytes", (uintptr_t)&xStreamBufferNextMessageLengthBytes },
    { "xStreamBufferReceive", (uintptr_t)&xStreamBufferReceive },
    { "xStreamBufferReceiveCompletedFromISR", (uintptr_t)&xStreamBufferReceiveCompletedFromISR },
    { "xStreamBufferReceiveFromISR", (uintptr_t)&xStreamBufferReceiveFromISR },
    { "xStreamBufferReset", (uintptr_t)&xStreamBufferReset },
    { "xStreamBufferSend", (uintptr_t)&xStreamBufferSend },
    { "xStreamBufferSendCompletedFromISR", (uintptr_t)&xStreamBufferSendCompletedFromISR },
    { "xStreamBufferSendFromISR", (uintptr_t)&xStreamBufferSendFromISR },
    { "xStreamBufferSetTriggerLevel", (uintptr_t)&xStreamBufferSetTriggerLevel },
    { "xStreamBufferSpacesAvailable", (uintptr_t)&xStreamBufferSpacesAvailable },
    { "port_uxCoreStartupDone", (uintptr_t)&port_uxCoreStartupDone },
    { "port_uxCriticalNesting", (uintptr_t)&port_uxCriticalNesting },
    { "port_uxInterruptNesting", (uintptr_t)&port_uxInterruptNesting },
    { "port_uxOldInterruptState", (uintptr_t)&port_uxOldInterruptState },
    { "port_xSchedulerRunning", (uintptr_t)&port_xSchedulerRunning },
    { "pxPortInitialiseStack", (uintptr_t)&pxPortInitialiseStack },
    { "vPortAssertIfInISR", (uintptr_t)&vPortAssertIfInISR },
    { "vPortClearInterruptMaskFromISR", (uintptr_t)&vPortClearInterruptMaskFromISR },
    { "vPortEndScheduler", (uintptr_t)&vPortEndScheduler },
    { "vPortEnterCritical", (uintptr_t)&vPortEnterCritical },
    { "vPortExitCritical", (uintptr_t)&vPortExitCritical },
    { "vPortSetStackWatchpoint", (uintptr_t)&vPortSetStackWatchpoint },
    { "vPortTCBPreDeleteHook", (uintptr_t)&vPortTCBPreDeleteHook },
    { "vPortYield", (uintptr_t)&vPortYield },
    { "vPortYieldFromISR", (uintptr_t)&vPortYieldFromISR },
    { "vPortYieldOtherCore", (uintptr_t)&vPortYieldOtherCore },
    { "xIsrStack", (uintptr_t)&xIsrStack },
    { "xIsrStackBottom", (uintptr_t)&xIsrStackBottom },
    { "xIsrStackTop", (uintptr_t)&xIsrStackTop },
    { "xPortGetTickRateHz", (uintptr_t)&xPortGetTickRateHz },
    { "xPortInIsrContext", (uintptr_t)&xPortInIsrContext },
    { "xPortInterruptedFromISRContext", (uintptr_t)&xPortInterruptedFromISRContext },
    { "xPortSetInterruptMaskFromISR", (uintptr_t)&xPortSetInterruptMaskFromISR },
    { "xPortStartScheduler", (uintptr_t)&xPortStartScheduler },
    { "xPortSwitchFlag", (uintptr_t)&xPortSwitchFlag },
    { "rtos_int_enter", (uintptr_t)&rtos_int_enter },
    { "rtos_int_exit", (uintptr_t)&rtos_int_exit },
    { "vEventGroupDeleteWithCaps", (uintptr_t)&vEventGroupDeleteWithCaps },
    { "xEventGroupCreateWithCaps", (uintptr_t)&xEventGroupCreateWithCaps },
    { "vQueueDeleteWithCaps", (uintptr_t)&vQueueDeleteWithCaps },
    { "vSemaphoreDeleteWithCaps", (uintptr_t)&vSemaphoreDeleteWithCaps },
    { "vStreamBufferGenericDeleteWithCaps", (uintptr_t)&vStreamBufferGenericDeleteWithCaps },
    { "vTaskDeleteWithCaps", (uintptr_t)&vTaskDeleteWithCaps },
    { "xQueueCreateWithCaps", (uintptr_t)&xQueueCreateWithCaps },
    { "xSemaphoreCreateGenericWithCaps", (uintptr_t)&xSemaphoreCreateGenericWithCaps },
    { "xStreamBufferGenericCreateWithCaps", (uintptr_t)&xStreamBufferGenericCreateWithCaps },
    { "xTaskCreatePinnedToCoreWithCaps", (uintptr_t)&xTaskCreatePinnedToCoreWithCaps },
    /* --- end of FreeRTOS kernel group (222 exported).
     * Dropped in ESP-IDF v6.0 (were already macros/removed upstream, no
     * linkable symbol left): ulTaskNotifyTake, vTaskDelayUntil,
     * xQueueGenericReceive, xTaskNotifyWait. Use ulTaskNotifyTake/xTaskDelayUntil
     * macros and xQueueReceive/xQueuePeek/xQueueSemaphoreTake instead.
     * Deliberately not exported (debugger-only struct, kernel-internal counter and an application-provided hook (must be defined by the app, never imported from the firmware) respectively):
     * FreeRTOS_openocd_params, uxTopUsedPriority, vApplicationStackOverflowHook. */
    { NULL, (uintptr_t)0 },
};

uintptr_t nnode_sym_resolve(const char *name)
{
    const nnode_sym_t *s;
    if (!name) {
        return 0;
    }
    for (s = kNNodeSyms; s->name; s++) {
        if (nnode_streq(s->name, name)) {
            return s->addr;
        }
    }
    return 0;
}

/* Loader-private API (elf_loader include/private/elf_symbol.h,
 * not exposed via public INCLUDE_DIRS; symbols are global). */
typedef uintptr_t (*nnode_symbol_resolver_fn)(const char *sym_name);
extern void elf_set_symbol_resolver(nnode_symbol_resolver_fn resolver);
extern uintptr_t elf_find_sym_default(const char *sym_name);

static uintptr_t nnode_resolver(const char *name)
{
    uintptr_t addr = nnode_sym_resolve(name);
    if (addr) {
        return addr;
    }
    return elf_find_sym_default(name);
}

void nnode_sym_resolver_install(void)
{
    elf_set_symbol_resolver(nnode_resolver);
}
