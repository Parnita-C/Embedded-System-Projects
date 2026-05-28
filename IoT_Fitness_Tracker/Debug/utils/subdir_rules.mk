################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
utils/%.obj: ../utils/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: Arm Compiler'
	"C:/TI/ccs1040/ccs/tools/compiler/ti-cgt-arm_20.2.5.LTS/bin/armcl" -mv7M4 --code_state=16 --float_support=none -me -Ooff --include_path="C:/Users/chpar/EEC 172/Final/IoT_Fitness_Tracker" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/simplelink/" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/simplelink/include" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/simplelink/source" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/driverlib/" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/inc/" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/example/common/" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/simplelink_extlib/provisioninglib" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/oslib" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/simplelink/ccs/OS" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/driverlib" --include_path="C:/TI/CC3200SDK_1.4.0/cc3200-sdk/example/common" --include_path="C:/TI/ccs1040/ccs/tools/compiler/ti-cgt-arm_20.2.5.LTS/include" --define=ccs --define=cc3200 --define=PLATFORM_LP --define=__CCS__ -g --diag_warning=225 --diag_wrap=off --display_error_number --abi=eabi --preproc_with_compile --preproc_dependency="utils/$(basename $(<F)).d_raw" --obj_directory="utils" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


