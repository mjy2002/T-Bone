//here we go with the real homing action
char find_sg_value() {
  writeRegister(motor_to_test, TMC4361_START_CONFIG_REGISTER,0 
    | _BV(5) //start is start
  | _BV(0) //X_TARGET needs start
  | _BV(1) //V_MAX needs start
  ); 
  writeRegister(motor_to_test, TMC4361_RAMP_MODE_REGISTER,0); //nice S-Ramps in velocity mode

  motors[motor_to_test].tmc260.setReadStatus(TMC26X_READOUT_STALLGUARD);
  set260Register(motor_to_test,motors[motor_to_test].tmc260.getDriverConfigurationRegisterValue() | 0x80);

  writeRegister(motor_to_test, TMC4361_A_MAX_REGISTER, FIXED_22_2_MAKE(fast_run*10));
  writeRegister(motor_to_test, TMC4361_BOW_1_REGISTER, fast_run*10);
  writeRegister(motor_to_test, TMC4361_BOW_2_REGISTER, fast_run*10);
  writeRegister(motor_to_test, TMC4361_BOW_3_REGISTER, fast_run*10);
  writeRegister(motor_to_test, TMC4361_BOW_4_REGISTER, fast_run*10);


  //char threshold=-63;
  char threshold=2; //we know it is at two - some speedup 
  for (;threshold<=64;threshold++) {
    long search_speed = (threshold%2!=0)? slow_run: -slow_run;
    writeRegister(motor_to_test, TMC4361_V_MAX_REGISTER, FIXED_23_8_MAKE(search_speed));
    signal_start();

    //Wait until we have speeded up enough
    unsigned long status = readRegister(motor_to_test,TMC4361_STATUS_REGISTER);
    while (status & _BV(2)==0) {
      status = readRegister(motor_to_test,TMC4361_STATUS_REGISTER);
    }

    motors[motor_to_test].tmc260.setStallGuardThreshold(threshold,1);
    set260Register(motor_to_test,motors[motor_to_test].tmc260.getStallGuard2RegisterValue());

    long x_actual = readRegister(motor_to_test,TMC4361_X_ACTUAL_REGISTER);
    //wait some full steps
    long new_x_actual = readRegister(motor_to_test,TMC4361_X_ACTUAL_REGISTER);
    while (abs(new_x_actual-x_actual)<microsteps*9) {
      new_x_actual = readRegister(motor_to_test,TMC4361_X_ACTUAL_REGISTER);
    } 
    long result = (long)readRegister(motor_to_test,TMC4361_COVER_DRIVER_LOW_REGISTER);
    result = (result >> 10);
    if (result > 0) {
      threshold -=1;
      return threshold;
    } 
  }
  Serial.println();

  writeRegister(motor_to_test, TMC4361_V_MAX_REGISTER, FIXED_23_8_MAKE(0));
  signal_start();

  writeRegister(motor_to_test, TMC4361_RAMP_MODE_REGISTER,_BV(2) | 2); //we want to go to positions in nice S-Ramps
}

void home_on_sg() {
  //find the SG value
  char threshold = find_sg_value();
  Serial.print("SG optimal at ");
  Serial.println(threshold,DEC);


  //Set the SG value for readout
  motors[motor_to_test].tmc260.setStallGuardThreshold(threshold,1);
  set260Register(motor_to_test,motors[motor_to_test].tmc260.getStallGuard2RegisterValue());

  //configure stall gaurd stop in squirrel  
  writeRegister(motor_to_test, TMC4361_V_STALL_LIMIT_REGISTER, fast_run-1l); //consider stall guard only if speed is high enough

  //we write down where we started
  long starting_point = readRegister(motor_to_test,TMC4361_X_ACTUAL_REGISTER);

  long left_stop = find_end_stop(true);
  long right_stop = find_end_stop(false);

  Serial.print("homed from ");
  Serial.print(starting_point);
  Serial.print(" to ");
  Serial.print(left_stop);
  Serial.print(" to ");
  Serial.println(right_stop);
  
  long middle_pos = left_stop + (right_stop - left_stop)/2;
  go_to(middle_pos);
}

long find_end_stop(boolean left) {
  //and again we go in velocity mode
  writeRegister(motor_to_test, TMC4361_RAMP_MODE_REGISTER,0); //nice S-Ramps in velocity mode

  long starting_point = readRegister(motor_to_test,TMC4361_X_ACTUAL_REGISTER);
  long stop = starting_point;

  writeRegister(motor_to_test, TMC4361_REFERENCE_CONFIG_REGISTER, TMC4361_STOP_ON_STALL_ENDSTOP_REGISTER_PATTERN | TMC4361_DRIVE_AFTER_STALL_ENDSTOP_REGISTER_PATTERN); //somebody may want to move later
  writeRegister(motor_to_test, TMC4361_REFERENCE_CONFIG_REGISTER, TMC4361_STOP_ON_STALL_ENDSTOP_REGISTER_PATTERN); //stop on stall guard

  long speed = left ? -fast_run : fast_run;

  Serial.print("Homing with ");
  Serial.println(speed);

  writeRegister(motor_to_test, TMC4361_V_MAX_REGISTER, FIXED_23_8_MAKE(speed));
  signal_start();


  //Wait until we have speeded up enough
  unsigned long status = readRegister(motor_to_test,TMC4361_STATUS_REGISTER);
  while ((status & TMC4361_STOP_ON_STALL_STATUS_REGISTER_PATTERN) == 0) {
    status = readRegister(motor_to_test,TMC4361_STATUS_REGISTER);
    stop = readRegister(motor_to_test,TMC4361_X_ACTUAL_REGISTER);
    if (abs(stop-starting_point)>homed_too_far) {
      Serial.print(stop);
      Serial.println(" is too far");
      stop = starting_point;
      break;
    }
  }
  Serial.print("settling at status ");
  Serial.println(status,HEX);

  //finally stop
  writeRegister(motor_to_test, TMC4361_V_MAX_REGISTER, 0);
  signal_start();

  writeRegister(motor_to_test, TMC4361_REFERENCE_CONFIG_REGISTER, TMC4361_STOP_ON_STALL_ENDSTOP_REGISTER_PATTERN | TMC4361_DRIVE_AFTER_STALL_ENDSTOP_REGISTER_PATTERN); //somebody may want to move later
  writeRegister(motor_to_test, TMC4361_REFERENCE_CONFIG_REGISTER, 0); //back to default

  long normal_pos = left ? stop+microsteps*9 : stop-microsteps*9;
  
  go_to(normal_pos);

  return stop;
}

void go_to(long position) {

  writeRegister(motor_to_test, TMC4361_RAMP_MODE_REGISTER,_BV(2) | 2); //we want to go to positions in nice S-Ramps

  //go back to remove the stall status
  writeRegister(motor_to_test, TMC4361_V_MAX_REGISTER, FIXED_23_8_MAKE(fast_run));
  signal_start();
  writeRegister(motor_to_test, TMC4361_X_TARGET_REGISTER, position);
  unsigned long status = readRegister(motor_to_test,TMC4361_STATUS_REGISTER);
  while ((status & _BV(0)) == 0) {
    status = readRegister(motor_to_test,TMC4361_STATUS_REGISTER);
  }
}
























