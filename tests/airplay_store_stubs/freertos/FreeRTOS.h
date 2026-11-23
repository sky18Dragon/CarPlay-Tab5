#pragma once
/* Single-threaded host stand-ins. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
typedef int BaseType_t;
