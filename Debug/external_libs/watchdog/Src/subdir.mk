################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../external_libs/watchdog/Src/app_watchdog.c 

OBJS += \
./external_libs/watchdog/Src/app_watchdog.o 

C_DEPS += \
./external_libs/watchdog/Src/app_watchdog.d 


# Each subdirectory must supply rules for building sources it contributes
external_libs/watchdog/Src/%.o external_libs/watchdog/Src/%.su external_libs/watchdog/Src/%.cyclo: ../external_libs/watchdog/Src/%.c external_libs/watchdog/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -c -I../Core/Inc -I../Drivers/STM32WLxx_HAL_Driver/Inc -I../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../Drivers/CMSIS/Include -I../LoRaWAN/App -I../LoRaWAN/Target -I../Utilities/trace/adv_trace -I../Utilities/misc -I../Utilities/sequencer -I../Utilities/timer -I../Utilities/lpm/tiny_lpm -I../Middlewares/Third_Party/LoRaWAN/LmHandler/Packages -I../Middlewares/Third_Party/LoRaWAN/Crypto -I../Middlewares/Third_Party/LoRaWAN/Mac/Region -I../Middlewares/Third_Party/LoRaWAN/Mac -I../Middlewares/Third_Party/LoRaWAN/LmHandler -I../Middlewares/Third_Party/LoRaWAN/Utilities -I../Middlewares/Third_Party/SubGHz_Phy -I../Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/Wake_Up_Button/Inc" -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/MFRC522/Inc" -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/persistent_circular_buffer/Inc" -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/adc_bat_meas/Inc" -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/lora_app_auxilary/Inc" -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/watchdog/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-external_libs-2f-watchdog-2f-Src

clean-external_libs-2f-watchdog-2f-Src:
	-$(RM) ./external_libs/watchdog/Src/app_watchdog.cyclo ./external_libs/watchdog/Src/app_watchdog.d ./external_libs/watchdog/Src/app_watchdog.o ./external_libs/watchdog/Src/app_watchdog.su

.PHONY: clean-external_libs-2f-watchdog-2f-Src

