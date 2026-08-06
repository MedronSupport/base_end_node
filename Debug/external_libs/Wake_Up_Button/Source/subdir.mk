################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../external_libs/Wake_Up_Button/Source/wake_up_button.c 

OBJS += \
./external_libs/Wake_Up_Button/Source/wake_up_button.o 

C_DEPS += \
./external_libs/Wake_Up_Button/Source/wake_up_button.d 


# Each subdirectory must supply rules for building sources it contributes
external_libs/Wake_Up_Button/Source/%.o external_libs/Wake_Up_Button/Source/%.su external_libs/Wake_Up_Button/Source/%.cyclo: ../external_libs/Wake_Up_Button/Source/%.c external_libs/Wake_Up_Button/Source/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -c -I../Core/Inc -I../Drivers/STM32WLxx_HAL_Driver/Inc -I../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../Drivers/CMSIS/Include -I../LoRaWAN/App -I../LoRaWAN/Target -I../Utilities/trace/adv_trace -I../Utilities/misc -I../Utilities/sequencer -I../Utilities/timer -I../Utilities/lpm/tiny_lpm -I../Middlewares/Third_Party/LoRaWAN/LmHandler/Packages -I../Middlewares/Third_Party/LoRaWAN/Crypto -I../Middlewares/Third_Party/LoRaWAN/Mac/Region -I../Middlewares/Third_Party/LoRaWAN/Mac -I../Middlewares/Third_Party/LoRaWAN/LmHandler -I../Middlewares/Third_Party/LoRaWAN/Utilities -I../Middlewares/Third_Party/SubGHz_Phy -I../Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/Wake_Up_Button/Inc" -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/MFRC522/Inc" -I"C:/Users/devic/Desktop/LW_RFID/base_end_node/external_libs/persistent_circular_buffer/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-external_libs-2f-Wake_Up_Button-2f-Source

clean-external_libs-2f-Wake_Up_Button-2f-Source:
	-$(RM) ./external_libs/Wake_Up_Button/Source/wake_up_button.cyclo ./external_libs/Wake_Up_Button/Source/wake_up_button.d ./external_libs/Wake_Up_Button/Source/wake_up_button.o ./external_libs/Wake_Up_Button/Source/wake_up_button.su

.PHONY: clean-external_libs-2f-Wake_Up_Button-2f-Source

