#include "main.h"

// Declare system global variable structure
system_t sys;
int32_t sys_position[N_AXIS];      // Real-time machine (aka home) position vector in steps.
int32_t sys_probe_position[N_AXIS]; // Last probe position in machine coordinates and steps.
volatile uint8_t sys_probe_state;   // Probing state value.  Used to coordinate the probing cycle with stepper ISR.
volatile uint8_t sys_rt_exec_state;   // Global realtime executor bitflag variable for state management. See EXEC bitmasks.
volatile uint8_t sys_rt_exec_alarm;   // Global realtime executor bitflag variable for setting various alarms.
volatile uint8_t sys_rt_exec_motion_override; // Global realtime executor bitflag variable for motion-based overrides.
volatile uint8_t sys_rt_exec_accessory_override; // Global realtime executor bitflag variable for spindle/coolant overrides.
#ifdef DEBUG
  volatile uint8_t sys_rt_exec_debug;
#endif

#if defined(USE_FREERTOS_RTOS)
  TaskHandle_t grbl_task_handler;
#endif

int main() {

  grbl_hw_init();

  client_init();

#ifdef STM32G0B0xx
  // here must wait for some time, beacuse STM32G0B0CE have no XTAL
  HAL_Delay(100);  
#endif

  grbl_report_mcu_info();

#ifdef DEBUG_TEST
  while(1) {
    

  }
#endif

#if defined(USE_FREERTOS_RTOS)
  xTaskCreate(enter_grbl_task, "grbl task", 1024, NULL, 1, &grbl_task_handler);
#else 
  enter_grbl_task();
#endif

#if defined(USE_FREERTOS_RTOS)
  osKernelStart();
#endif
}

#if defined(USE_FREERTOS_RTOS)
void enter_grbl_task(void *parg) {
#else 
void enter_grbl_task(void) {
#endif

  hal_flash_unlock();
	hal_eeprom_init();

  // Initialize system upon power-up.
	serial_init();   // Setup serial baud rate and interrupts
	settings_init(); // Load Grbl settings from EEPROM
	stepper_init();  // Configure stepper pins and interrupt timers
	system_init();   // Configure pinout pins and pin-change interrupt

	memset(sys_position,0,sizeof(sys_position)); // Clear machine position.

	// Initialize system state.
#ifdef FORCE_INITIALIZATION_ALARM
	  // Force Grbl into an ALARM state upon a power-cycle or hard reset.
	sys.state = STATE_ALARM;
#else
	sys.state = STATE_IDLE;
#endif
	// Check for power-up and set system alarm if homing is enabled to force homing cycle
	// by setting Grbl's alarm state. Alarm locks out all g-code commands, including the
	// startup scripts, but allows access to settings and internal commands. Only a homing
	// cycle '$H' or kill alarm locks '$X' will disable the alarm.
	// NOTE: The startup script will run after successful completion of the homing cycle, but
	// not after disabling the alarm locks. Prevents motion startup blocks from crashing into
	// things uncontrollably. Very bad.
#ifdef HOMING_INIT_LOCK
	if (bit_istrue(settings.flags, BITFLAG_HOMING_ENABLE)) { sys.state = STATE_ALARM; }
#endif
	// Grbl initialization loop upon power-up or a system abort. For the latter, all processes
  // will return to this loop to be cleanly re-initialized.

	for(;;) {
        // Reset system variables.
        uint8_t prior_state = sys.state;
        memset(&sys, 0, sizeof(system_t)); // Clear system struct variable.
        sys.state = prior_state;
        sys.f_override = DEFAULT_FEED_OVERRIDE;  // Set to 100%
        sys.r_override = DEFAULT_RAPID_OVERRIDE; // Set to 100%
        sys.spindle_speed_ovr = DEFAULT_SPINDLE_SPEED_OVERRIDE; // Set to 100%
        memset(sys_probe_position,0,sizeof(sys_probe_position)); // Clear probe position.
        sys_probe_state = 0;
        sys_rt_exec_state = 0;
        sys_rt_exec_alarm = 0;
        sys_rt_exec_motion_override = 0;
        sys_rt_exec_accessory_override = 0;

        // Reset Grbl primary systems.
        serial_reset_read_buffer(); // Clear serial read buffer
        gc_init(); // Set g-code parser to default state
        spindle_init();
        coolant_init();
        limits_init();
        probe_init();
        plan_reset(); // Clear block buffer and planner variables
        st_reset(); // Clear stepper subsystem variables.

        // Sync cleared gcode and planner positions to current system position.
        plan_sync_position();
        gc_sync_position();
        // Print welcome message. Indicates an initialization has occured at power-up or with a reset.
        report_init_message();
        // Start Grbl main loop. Processes program inputs and executes them.
        protocol_main_loop();
    }	
}

void SysTick_Handler(void)
{
  HAL_IncTick();

#if defined(USE_FREERTOS_RTOS)

#if (INCLUDE_xTaskGetSchedulerState == 1 )
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
#endif /* INCLUDE_xTaskGetSchedulerState */
  xPortSysTickHandler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  }
#endif /* INCLUDE_xTaskGetSchedulerState */

#endif
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

void _delay_ms(uint32_t tick) {
  
#if defined(USE_FREERTOS_RTOS)
  vTaskDelay(tick);
#else 
  uint32_t mililoop = SystemCoreClock / 1000;
	for (uint32_t i=0; i< mililoop; i++)
		__asm__ __volatile__("nop\n\t":::"memory");
  // HAL_Delay(tick);
#endif
}

void _delay_us(uint32_t tick) {
	__NOP();
}
