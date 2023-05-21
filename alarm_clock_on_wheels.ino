/* Copyright Podaru Andrei-Alexandru 333CA 2023 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DS3231.h>

// define states in our FSM
#define INITIAL_STATE 0
#define SETTING_ALARM 1
#define ALARM_SET 2
#define ALARM_RINGING 3

// define pins that are used
#define in1 5
#define in2 6
#define in3 7
#define in4 8
#define buzzer_pin 9
#define enA 11
#define enB 12

// utils
#define MINIMUM_TIME_BETWEEN_BUTTON_PRESSES_MILLIS 200
#define NUMBER_OF_SECONDS_IN_A_MINUTE 60
#define DEFAULT_SNOOZE_TIME_MINUTES 3
#define BUZZING_TIME_MILLIS 3500
#define BUZZER_FREQUENCY_HZ 1000

// for debouncing
volatile unsigned long last_yellow_button_push = 0;
volatile unsigned long last_red_button_push = 0;
volatile unsigned long last_blue_button_push = 0;

/*
 * Yellow button: increment number of minutes / snooze for 5 minutes
 * Red button: decrement number of minutes / stop alarm
 * Blue button: confirm
*/

// global variables
DS3231  rtc(SDA, SCL);
LiquidCrystal_I2C lcd(0x27, 16, 2);

volatile unsigned int seconds_passed = 0;
volatile unsigned int seconds_for_alarm_start = 0;
volatile unsigned int number_of_minutes_selected = 0;
volatile unsigned long current_direction_start;
volatile bool direction = false;

volatile int state = INITIAL_STATE;
volatile bool changed_state = false;
volatile bool force_lcd_clear = false;

// treat Timer1 compare interrupt request
ISR(TIMER1_COMPA_vect) {
  seconds_passed += 2;
  if (seconds_passed >= seconds_for_alarm_start && state == ALARM_SET) {
    state++;
    changed_state = true;
  }
}

// treat interrupt from blue button
ISR(INT0_vect) {
  unsigned long current_time = millis();
  if (current_time - last_blue_button_push >= MINIMUM_TIME_BETWEEN_BUTTON_PRESSES_MILLIS) {
    last_blue_button_push = current_time;

    // confirm number of minutes selected
    if (state == SETTING_ALARM) {
      state++;
      changed_state = true;
      seconds_for_alarm_start = seconds_passed + NUMBER_OF_SECONDS_IN_A_MINUTE * number_of_minutes_selected;
    }
  }
}

// treat interrupt from red button
ISR(INT1_vect) {
  unsigned long current_time = millis();
  if (current_time - last_red_button_push >= MINIMUM_TIME_BETWEEN_BUTTON_PRESSES_MILLIS) {
    last_red_button_push = current_time;

    // decrease the number of minutes
    if (state == SETTING_ALARM && number_of_minutes_selected > 0) {
      number_of_minutes_selected--;
      if (number_of_minutes_selected == 9 || number_of_minutes_selected == 99)
        force_lcd_clear = true;
    }

    // stop alarm
    if (state == ALARM_RINGING) {
      state = INITIAL_STATE;
      changed_state = true;
    }
  }
}

// treat interrupt from yellow button
ISR(PCINT2_vect) {
  // check that interrupt comes from PD4
  if ((PIND & (1 << PD4)) == 0) {
    unsigned long current_time = millis();
    if (current_time - last_yellow_button_push >= MINIMUM_TIME_BETWEEN_BUTTON_PRESSES_MILLIS) {
      last_yellow_button_push = current_time;

      // increase the number of minutes
      if (state == SETTING_ALARM) {
        number_of_minutes_selected++;
      }

      // enter alarm set mode
      if (state == INITIAL_STATE) {
        state++;
        changed_state = true;
        number_of_minutes_selected = 1;
      }

      // snooze alarm for 5 minutes
      if (state == ALARM_RINGING) {
        state = ALARM_SET;
        changed_state = true;
        seconds_for_alarm_start = seconds_passed + DEFAULT_SNOOZE_TIME_MINUTES * NUMBER_OF_SECONDS_IN_A_MINUTE;
      }
    }
  }
}

/*
 * Configure Timer1 in CTC mode with a frequency of 0.5Hz
 * so that an interrupt is made every 2 seconds
*/
void configure_timer1() {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 31250;
  // CTC mode
  TCCR1B |= (1 << WGM12);
  // 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);
}

void configure_button_interrupts() {
  // rising edge of both INT0 and INT1 create interrupts
  EICRA |= (1 << ISC00) | (1 << ISC01) | (1 << ISC10) | (1 << ISC11);

  // activate INT0(blue button) and INT1(red button)
  EIMSK |= (1 << INT0) | (1 << INT1);

  // set PCIE2 bit in PCICR to enable PCMSK2 scan
  PCICR |= (1 << PCIE2);

  // activate PCINT20(yellow button)
  PCMSK2 |= (1 << PCINT20);
}

void setup() {
  // set pin modes
  pinMode(buzzer_pin, OUTPUT);
  pinMode(enA, OUTPUT);
  pinMode(enB, OUTPUT);
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);

  // setup RTC module and LCD screen
  rtc.begin();
  lcd.begin();
  lcd.backlight();
  lcd.clear();

  // set up timer
  // deactivate interrupts
  cli();
  configure_timer1();
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);
  // activate interrupts
  sei();

  configure_button_interrupts();
}

/*
 * Print current date on the first row and current time on the second row
 * of the 16x2 LCD display
*/
void print_date_and_time() {
  // day of week
  char *day_of_week = rtc.getDOWStr();
  lcd.setCursor(0, 0);
  lcd.print(day_of_week[0]);
  lcd.print(day_of_week[1]);
  lcd.print(day_of_week[2]);
  lcd.print(" ");
  
  // date
  lcd.print(rtc.getDateStr());

  // current time
  lcd.setCursor(0, 1);
  lcd.print(rtc.getTimeStr());
}

/*
 * Print the number of minutes selected by the user for alarm
*/
void print_current_alarm_info() {
  lcd.setCursor(0, 0);
  lcd.print("Set alarm for:");

  lcd.setCursor(0, 1);

  // print number of minutes on the second row
  char buff[40];
  sprintf(buff, "%d minutes", number_of_minutes_selected);
  lcd.print(buff);
}

/*
 * Print instructions when alarm is ringing
*/
void print_during_alarm_ringing() {
  lcd.setCursor(0, 0);
  char buff[40];
  sprintf(buff, "Yellow: snooze %d", DEFAULT_SNOOZE_TIME_MINUTES);
  lcd.print(buff);

  lcd.setCursor(0, 1);
  lcd.print("Red: stop alarm");
}

void move_forward() {
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);
 
  analogWrite(enA, 200);
  analogWrite(enB, 200);
}

void move_backward() {
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
 
  analogWrite(enA, 200);
  analogWrite(enB, 200);
}

void stop_car() {
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);  
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);
}

void loop() {
  // clear the display on each change of state
  if (changed_state) {
    lcd.clear();
    changed_state = false;

    if (state == ALARM_RINGING) {
      direction = false;
      current_direction_start = millis();
      return;
    } else {
      stop_car();
    }
  }

  // sound the buzzer and print the 'snooze' and 'stop alarm' options
  // while moving the car back and forth
  if (state == ALARM_RINGING) {
    print_during_alarm_ringing();

    if (millis() - current_direction_start >= BUZZING_TIME_MILLIS) {
      // change the direction in which the car is moving
      current_direction_start = millis();
      direction = !direction;
      return;
    }

    if (direction) {
      tone(buzzer_pin, BUZZER_FREQUENCY_HZ);
      move_forward();
    } else {
      noTone(buzzer_pin);
      move_backward();
    }
    return;
  }

  // print the date and time
  if (state == INITIAL_STATE || state == ALARM_SET) {
    noTone(buzzer_pin);
    print_date_and_time();
    return;
  }

  // while setting the alarm, print the number of minutes selected
  if (state == SETTING_ALARM) {
    if (force_lcd_clear) {
      lcd.clear();
      force_lcd_clear = false;
    }

    print_current_alarm_info();
  }
}
