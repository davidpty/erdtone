//*****************************************************************************
// Title        : Pulse to tone (DTMF) converter
// Author       : Boris Cherkasskiy
//                http://boris0.blogspot.ca/2013/09/rotary-dial-for-digital-age.html
// Created      : 2011-10-24
//
// Modified     : Arnie Weber 2015-06-22
//                https://bitbucket.org/310weber/rotary_dial/
//                NOTE: This code is not compatible with Boris's original hardware
//                due to changed pin-out (see Eagle files for details)
//
// Modified     : Matthew Millman 2018-05-29
//                http://tech.mattmillman.com/
//                Cleaned up implementation, modified to work more like the
//                Rotatone commercial product.
//
// Modified     : davidpty 2026-04-23
//                added rotatone v2 hotdial and support for 'Erdtaste' 
//
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//
// DTMF generator logic is loosely based on the AVR314 app note from Atmel
//
//*****************************************************************************

// Uncomment to build with reverse dial
//#define NZ_DIAL

#include <stdbool.h>
#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <avr/eeprom.h>

#include "dtmf.h" 

#define PIN_DIAL                    PB1
#define PIN_PULSE                   PB2

#define SPEED_DIAL_SIZE             32

#define STATE_DIAL                  0x00
#define STATE_SPECIAL_L1            0x01
#define STATE_SPECIAL_L2            0x02
#define STATE_PROGRAM_SD            0x03
#define STATE_HOTLINE_SLOT          0x04
#define STATE_HOTLINE_DELAY         0x05

#define F_NONE                      0x00
#define F_DETECT_SPECIAL_L1         0x01
#define F_DETECT_SPECIAL_L2         0x02
#define F_WDT_AWAKE                 0x04

#define SLEEP_64MS                  0x00
#define SLEEP_128MS                 0x01
#define SLEEP_1S                    0x02
#define SLEEP_2S                    0x03

#define SPECIAL_L1_HOLD_TIME        SLEEP_1S
#define SPECIAL_L2_HOLD_TIME        SLEEP_2S

#define DTMF_DURATION_MS             100
#define DTMF_PAUSE_MS              2000  // Pause inserted in speed dial memory

#define SPEED_DIAL_COUNT            8 // 8 Positions in total (Redail(3),4,5,6,7,8,9,0)
#define SPEED_DIAL_REDIAL           (SPEED_DIAL_COUNT - 1)

#define L2_STAR                     1
#define L2_POUND                    2
#define L2_REDIAL                   3

typedef struct
{
    uint8_t enabled;
    uint8_t slot_digit;
    uint8_t delay_digit;
} hotline_config_t;

typedef struct
{
    uint8_t state;
    uint8_t flags;
    bool dial_pin_state;
    uint8_t special_hold_state;
    // Suppresses redial on first Erdtaste release after boot.
    // Set when hotline is canceled OR when hotline dialed.
    bool suppress_first_release;
    // Set when a long hold is being used to enter a literal `*` or `#` while programming.
    bool program_special_insert;
    uint8_t speed_dial_index;
    uint8_t speed_dial_digit_index;
    uint8_t hotline_slot_digit;
    int8_t speed_dial_digits[SPEED_DIAL_SIZE];
    int8_t dialed_digit;
} runstate_t;

static void init(void);
static void process_dialed_digit(runstate_t *rs);
static void load_hotline_config(hotline_config_t *config);
static void write_hotline_config(const hotline_config_t *config);
static uint16_t hotline_delay_ms(uint8_t delay_digit);
static bool wait_for_hotline_window(uint16_t delay_ms);
static void suppress_first_release_if_needed(runstate_t *rs, bool dial_pin_state);
static void maybe_dial_hotline(void);
static void dial_speed_dial_number(int8_t *speed_dial_digits, int8_t index, bool save_to_redial);
static void write_current_speed_dial(int8_t *speed_dial_digits, int8_t index);
static void wdt_timer_start(uint8_t delay);
static void start_sleep(void);
static void wdt_stop(void);

// Map speed dial numbers to memory locations
const int8_t _g_speed_dial_loc[] =
{
    0,
    -1 /* 1 - * */,
    -1 /* 2 - # */,
    -1 /* 3 - Redial */,
    1,
    2,
    3,
    4,
    5,
    6 
};

int8_t EEMEM _g_speed_dial_eeprom[SPEED_DIAL_COUNT][SPEED_DIAL_SIZE] = { [0 ... (SPEED_DIAL_COUNT - 1)][0 ... SPEED_DIAL_SIZE - 1] = DIGIT_OFF };
hotline_config_t EEMEM _g_hotline_eeprom = { 0, 0, 5 };
runstate_t _g_run_state;

int main(void)
{
    runstate_t *rs = &_g_run_state;
    bool dial_pin_prev_state;

    init();

    // Wait for the decoupling capacitors to charge
    wdt_timer_start(SLEEP_128MS);
    start_sleep();
    wdt_stop();

    dtmf_init();

    // Local dial status variables 
    rs->state = STATE_DIAL;
    rs->dial_pin_state = true;
    rs->flags = F_NONE;
    rs->special_hold_state = STATE_DIAL;
    rs->suppress_first_release = false;
    rs->program_special_insert = false;
    rs->speed_dial_digit_index = 0;
    rs->speed_dial_index = 0;
    rs->hotline_slot_digit = DIGIT_OFF;
    dial_pin_prev_state = true;
    
    for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
        rs->speed_dial_digits[i] = DIGIT_OFF;

    maybe_dial_hotline();

    while (1)
    {
        rs->dial_pin_state = bit_is_set(PINB, PIN_DIAL);

        bool suppressed = rs->suppress_first_release;

        suppress_first_release_if_needed(rs, rs->dial_pin_state);
        // Skip if flag was set BEFORE clearing - prevents redial on first release
        if (suppressed)
        {
            dial_pin_prev_state = rs->dial_pin_state;
            continue;
        }

        if (dial_pin_prev_state != rs->dial_pin_state) 
        {
            if (!rs->dial_pin_state) 
            {
                // Dial just started
                // Enable special function detection
                rs->flags |= F_DETECT_SPECIAL_L1;
                rs->dialed_digit = 0;

                wdt_timer_start(SLEEP_64MS);
                start_sleep();
            }
            else 
            {
                // Disable SF detection (should be already disabled)
                rs->flags = F_NONE;

                // Check that we detect a valid digit
                if (rs->dialed_digit <= 0 || rs->dialed_digit > 10)
                {
                    rs->dialed_digit = DIGIT_OFF;

                    // No pulses detected - could be Erdtaste short press
                    // Only treat as redial if we are in normal dial state
                    // (not in special function mode which uses longer holds)
                    if (rs->state == STATE_DIAL && !rs->suppress_first_release)
                    {
                        // Erdtaste short press - trigger redial
                        eeprom_read_block(rs->speed_dial_digits, 
                            &_g_speed_dial_eeprom[SPEED_DIAL_REDIAL][0], 
                            SPEED_DIAL_SIZE);
                        dial_speed_dial_number(rs->speed_dial_digits, SPEED_DIAL_REDIAL, false);
                    }
                    else
                    {
                        rs->suppress_first_release = false;
                        wdt_timer_start(SLEEP_64MS);
                        start_sleep();
                    }
                }
                else 
                {
                    // Got a valid digit - process it            
#ifdef NZ_DIAL
                    // NZPO Phones only. 0 is same as GPO but 1-9 are reversed.
                    rs->dialed_digit = (10 - rs->dialed_digit);
#else
                    if (rs->dialed_digit == 10)
                        rs->dialed_digit = 0; // 10 pulses => 0
#endif
                    wdt_timer_start(SLEEP_128MS);
                    start_sleep();
                    wdt_stop();

                    process_dialed_digit(rs);
                }
            }    
        } 
        else 
        {
            if (rs->dial_pin_state) 
            {
                // Rotary dial at the rest position
                // Reset all variables
                rs->state = STATE_DIAL;
                rs->flags = F_NONE;
                rs->dialed_digit = DIGIT_OFF;
                rs->special_hold_state = STATE_DIAL;
            }
        }

        dial_pin_prev_state = rs->dial_pin_state;

        // Don't power down if special function detection is active        
        if (rs->flags & F_DETECT_SPECIAL_L1)
        {
            rs->flags &= ~F_WDT_AWAKE;
            // Put MCU to sleep - to be awoken either by pin interrupt or WDT
            wdt_timer_start(SPECIAL_L1_HOLD_TIME);
            start_sleep();

            // Special function mode detected?
            if (rs->flags & F_WDT_AWAKE)
            {
                // SF mode detected
                rs->flags &= ~F_WDT_AWAKE;
                rs->special_hold_state = rs->state;
                if (rs->state == STATE_PROGRAM_SD)
                    rs->program_special_insert = true;
                rs->state = STATE_SPECIAL_L1;
                rs->flags &= ~F_DETECT_SPECIAL_L1;
                rs->flags |= F_DETECT_SPECIAL_L2;

                // Indicate that we entered L1 SF mode with short beep
                dtmf_generate_tone(DIGIT_BEEP_LOW, 200);
            }
            else
            {
                // Released before the first timeout: cancel special detection so a
                // short press can be handled as redial on the next loop.
                rs->flags &= ~F_DETECT_SPECIAL_L1;
            }
        }
        else if (rs->flags & F_DETECT_SPECIAL_L2)
        {
            rs->flags &= ~F_WDT_AWAKE;
            // Put MCU to sleep - to be awoken either by pin interrupt or WDT
            wdt_timer_start(SPECIAL_L2_HOLD_TIME);
            start_sleep();

            if (rs->flags & F_WDT_AWAKE)
            {
                // SF mode detected
                rs->flags &= ~F_WDT_AWAKE;
                rs->special_hold_state = rs->state;
                rs->state = STATE_SPECIAL_L2;
                rs->flags &= ~F_DETECT_SPECIAL_L2;

                // Indicate that we entered L2 SF mode with asc tone
                dtmf_generate_tone(DIGIT_TUNE_ASC, 200);
            }
            else
            {
                // Released before the second timeout: cancel special detection.
                rs->flags &= ~F_DETECT_SPECIAL_L2;
            }
        }
        else
        {
            // Don't need timer - sleep to power down mode
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_mode();
        }
    }

    return 0;
}

static void process_dialed_digit(runstate_t *rs)
{
    if (rs->state == STATE_DIAL)
    {
        // Standard (no speed dial, no special function) mode
        // Generate DTMF code
        dtmf_generate_tone(rs->dialed_digit, DTMF_DURATION_MS);

        if (rs->dialed_digit == L2_STAR || rs->dialed_digit == L2_POUND)
        {
            // Store * or # in redial memory
            if (rs->speed_dial_digit_index < SPEED_DIAL_SIZE)
            {
                rs->speed_dial_digits[rs->speed_dial_digit_index] =
                    (rs->dialed_digit == L2_STAR) ? DIGIT_STAR : DIGIT_POUND;
                rs->speed_dial_digit_index++;
                write_current_speed_dial(rs->speed_dial_digits, SPEED_DIAL_REDIAL);
            }
        }
        else if (rs->speed_dial_digit_index < SPEED_DIAL_SIZE)
        {
            // During regular dial always save into the 'Redial' position of the speed dial memory
            rs->speed_dial_digits[rs->speed_dial_digit_index] = rs->dialed_digit;
            rs->speed_dial_digit_index++;
            
            write_current_speed_dial(rs->speed_dial_digits, SPEED_DIAL_REDIAL);
        }
    }
    else if (rs->state == STATE_SPECIAL_L1)
    {
        if (rs->program_special_insert)
        {
            if (rs->dialed_digit == L2_STAR || rs->dialed_digit == L2_POUND)
            {
                if (rs->speed_dial_digit_index >= SPEED_DIAL_SIZE)
                {
                    rs->state = STATE_DIAL;
                    rs->program_special_insert = false;
                    dtmf_generate_tone(DIGIT_TUNE_DESC, 800);
                    return;
                }

                rs->speed_dial_digits[rs->speed_dial_digit_index] =
                    (rs->dialed_digit == L2_STAR) ? DIGIT_STAR : DIGIT_POUND;
                rs->speed_dial_digit_index++;
                write_current_speed_dial(rs->speed_dial_digits, rs->speed_dial_index);
                dtmf_generate_tone(DIGIT_BEEP_LOW, DTMF_DURATION_MS);
                rs->state = STATE_PROGRAM_SD;
                rs->program_special_insert = false;
                return;
            }

            if (rs->dialed_digit == L2_REDIAL)
            {
                // Insert pause on dial 3
                if (rs->speed_dial_digit_index >= SPEED_DIAL_SIZE)
                {
                    rs->state = STATE_DIAL;
                    rs->program_special_insert = false;
                    dtmf_generate_tone(DIGIT_TUNE_DESC, 800);
                    return;
                }

                rs->speed_dial_digits[rs->speed_dial_digit_index] = DIGIT_PAUSE;
                rs->speed_dial_digit_index++;
                write_current_speed_dial(rs->speed_dial_digits, rs->speed_dial_index);
                dtmf_generate_tone(DIGIT_BEEP, DTMF_DURATION_MS);
                rs->state = STATE_PROGRAM_SD;
                rs->program_special_insert = false;
                return;
            }

            rs->program_special_insert = false;
        }

        if (rs->dialed_digit == L2_STAR)
        {
            // SF 1-*
            dtmf_generate_tone(DIGIT_STAR, DTMF_DURATION_MS);  
            rs->state = STATE_DIAL;
        }
        else if (rs->dialed_digit == L2_POUND)
        {
            // SF 2-#
            dtmf_generate_tone(DIGIT_POUND, DTMF_DURATION_MS);
            rs->state = STATE_DIAL;
        }
        else if (rs->dialed_digit == L2_REDIAL)
        {
            // SF 3 (Redial)
            dial_speed_dial_number(rs->speed_dial_digits, SPEED_DIAL_REDIAL, false);
        }
        else if (_g_speed_dial_loc[rs->dialed_digit] >= 0)
        {
            // Call speed dial number
            dial_speed_dial_number(rs->speed_dial_digits, _g_speed_dial_loc[rs->dialed_digit], true);
        }
    }
    else if (rs->state == STATE_SPECIAL_L2)
    {
        // Clear SD slot - hold 2nd beep then dial 0
        if (rs->program_special_insert)
        {
            if (rs->dialed_digit == 0)
            {
                for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
                    rs->speed_dial_digits[i] = DIGIT_OFF;

                rs->speed_dial_digit_index = 0;
                write_current_speed_dial(rs->speed_dial_digits, rs->speed_dial_index);
                dtmf_generate_tone(DIGIT_TUNE_DESC, 400);
                rs->state = STATE_PROGRAM_SD;
                rs->program_special_insert = false;
                return;
            }

            rs->program_special_insert = false;
        }

        // Clear hotline - hold 2nd beep at hotline slot then dial 0
        if (rs->special_hold_state == STATE_HOTLINE_SLOT && rs->dialed_digit == 0)
        {
            hotline_config_t config;

            load_hotline_config(&config);
            config.enabled = 0;
            config.slot_digit = DIGIT_OFF;
            config.delay_digit = DIGIT_OFF;
            write_hotline_config(&config);
            dtmf_generate_tone(DIGIT_TUNE_DESC, 800);
            rs->hotline_slot_digit = DIGIT_OFF;
            rs->state = STATE_DIAL;
            return;
        }

        if (rs->dialed_digit == L2_REDIAL)
        {
            rs->state = STATE_HOTLINE_SLOT;
        }
        else if (_g_speed_dial_loc[rs->dialed_digit] >= 0)
        {
            rs->speed_dial_index = _g_speed_dial_loc[rs->dialed_digit];
            rs->speed_dial_digit_index = 0;

            for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
                rs->speed_dial_digits[i] = DIGIT_OFF;

            rs->state = STATE_PROGRAM_SD;
        }
        else
        {
            rs->state = STATE_DIAL;
        }
    }
    else if (rs->state == STATE_PROGRAM_SD)
    {
        // Do we have too many digits entered?
        if (rs->speed_dial_digit_index >= SPEED_DIAL_SIZE)
        {
            // Exit speed dial mode
            rs->state = STATE_DIAL;
            // Beep to indicate that we done
            dtmf_generate_tone(DIGIT_TUNE_DESC, 800);
        } 
        else
        {
            // Next digit
            rs->speed_dial_digits[rs->speed_dial_digit_index] = rs->dialed_digit;
            rs->speed_dial_digit_index++;

            // Generic beep - do not gererate DTMF code
            dtmf_generate_tone(DIGIT_BEEP_LOW, DTMF_DURATION_MS);
        }

        // Write SD on every digit so user can hang up to save
        write_current_speed_dial(rs->speed_dial_digits, rs->speed_dial_index);
    }
    else if (rs->state == STATE_HOTLINE_SLOT)
    {
        if (_g_speed_dial_loc[rs->dialed_digit] >= 0)
        {
            // Select the stored number that will become the hotline number
            rs->hotline_slot_digit = rs->dialed_digit;
            rs->state = STATE_HOTLINE_DELAY;
            dtmf_generate_tone(DIGIT_BEEP_LOW, DTMF_DURATION_MS);

            // Save the default delay immediately unless the caller overrides it
            // with an explicit delay digit afterwards.
            hotline_config_t config;

            config.enabled = 1;
            config.slot_digit = rs->hotline_slot_digit;
            config.delay_digit = 5;
            write_hotline_config(&config);
        }
        else
        {
            rs->state = STATE_DIAL;
        }
    }
    else if (rs->state == STATE_HOTLINE_DELAY)
    {
        hotline_config_t config;

        // Hotline delay accepts 0..9:
        // 0 = 0 seconds, 1..9 = 1..9 seconds.
        if (rs->dialed_digit <= 9)
        {
            config.enabled = 1;
            config.slot_digit = rs->hotline_slot_digit;
            config.delay_digit = rs->dialed_digit;
            write_hotline_config(&config);
            dtmf_generate_tone(DIGIT_TUNE_ASC, 400);
        }
        else
        {
            dtmf_generate_tone(DIGIT_TUNE_DESC, 400);
        }

        rs->hotline_slot_digit = DIGIT_OFF;
        rs->state = STATE_DIAL;
    }
}

// Dial speed dial number (it erases current SD number in the global structure)
static void dial_speed_dial_number(int8_t *speed_dial_digits, int8_t index, bool save_to_redial)
{
    if (index >= 0 && index < SPEED_DIAL_COUNT)
    {
        eeprom_read_block(speed_dial_digits, &_g_speed_dial_eeprom[index][0], SPEED_DIAL_SIZE);

        // Also save to redial memory for convenient replay
        if (save_to_redial && index != SPEED_DIAL_REDIAL)
        {
            write_current_speed_dial(speed_dial_digits, SPEED_DIAL_REDIAL);
        }

        for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
        {
            // Skip invalid digits (negative or > 11, but allow DIGIT_PAUSE=12)
            if (speed_dial_digits[i] < 0 || (speed_dial_digits[i] > DIGIT_POUND && speed_dial_digits[i] != DIGIT_PAUSE))
                continue;

            // Handle pause
            if (speed_dial_digits[i] == DIGIT_PAUSE)
            {
                sleep_ms(DTMF_PAUSE_MS);
                continue;
            }

            // Send the tone
            dtmf_generate_tone(speed_dial_digits[i], DTMF_DURATION_MS);

            // Normal pause after tone (always added)
            sleep_ms(DTMF_DURATION_MS);
        }
    }
}

static void write_current_speed_dial(int8_t *speed_dial_digits, int8_t index)
{
    if (index >= 0 && index < SPEED_DIAL_COUNT)
    {
        // If dialed index SPEED_DIAL_FIRST => using array index 0
        eeprom_update_block(speed_dial_digits, &_g_speed_dial_eeprom[index][0], SPEED_DIAL_SIZE);
    }
}

static void load_hotline_config(hotline_config_t *config)
{
    eeprom_read_block(config, &_g_hotline_eeprom, sizeof(*config));
}

static void write_hotline_config(const hotline_config_t *config)
{
    eeprom_update_block(config, &_g_hotline_eeprom, sizeof(*config));
}

static uint16_t hotline_delay_ms(uint8_t delay_digit)
{
    static const uint16_t delay_table[] =
    {
        500,
        1000,
        2000,
        3000,
        4000,
        5000,
        6000,
        7000,
        8000,
        9000
    };

    if (delay_digit > 9)
        return 0;

    return delay_table[delay_digit];
}

static bool wait_for_hotline_window(uint16_t delay_ms)
{
    uint16_t elapsed = 0;

    while (elapsed < delay_ms)
    {
        if (!bit_is_set(PINB, PIN_DIAL))
        {
            return false;
        }

        wdt_timer_start(SLEEP_64MS);
        start_sleep();
        wdt_stop();
        elapsed += 64;
    }

    return true;
}

static void suppress_first_release_if_needed(runstate_t *rs, bool dial_pin_state)
{
    if (!rs->suppress_first_release)
        return;

    rs->state = STATE_DIAL;
    rs->flags = F_NONE;
    rs->dialed_digit = DIGIT_OFF;

    // Only the dial/Erdtaste line should release this latch.
    if (dial_pin_state)
        rs->suppress_first_release = false;
}

static void maybe_dial_hotline(void)
{
    hotline_config_t config;
    int8_t hotline_index;
    uint16_t delay_ms;

    load_hotline_config(&config);

    if (!config.enabled)
        return;

    if (config.slot_digit >= sizeof(_g_speed_dial_loc) / sizeof(_g_speed_dial_loc[0]))
        return;

    hotline_index = _g_speed_dial_loc[config.slot_digit];
    if (hotline_index < 0)
        return;

    delay_ms = hotline_delay_ms(config.delay_digit);

    // Check: Button held AT boot (before power-on)
    if (!bit_is_set(PINB, PIN_DIAL))
    {
        _g_run_state.suppress_first_release = true;
        return;  // hotline canceled
    }

    // Wait for delay window (0-9 seconds based on config)
    if (!wait_for_hotline_window(delay_ms))
    {
        // Released before delay window - cancel
        return;
    }

    // Held through delay - dial hotline
    dial_speed_dial_number(_g_run_state.speed_dial_digits, hotline_index, false);
    _g_run_state.suppress_first_release = true;
}

static void init(void)
{
    // Program clock prescaller to divide + frequency by 1
    // Write CLKPCE 1 and other bits 0    
    CLKPR = _BV(CLKPCE);

    // Write prescaler value with CLKPCE = 0
    CLKPR = 0;

    // Enable pull-ups
    PORTB |= (_BV(PIN_DIAL) | _BV(PIN_PULSE));

    // Disable unused modules to save power
    PRR = _BV(PRTIM1) | _BV(PRUSI) | _BV(PRADC);
    ACSR = _BV(ACD);

    // Configure pin change interrupt
    MCUCR = _BV(ISC01) | _BV(ISC00);         // Set INT0 for falling edge detection
    GIMSK = _BV(INT0) | _BV(PCIE);           // Added INT0
    PCMSK = _BV(PIN_DIAL) | _BV(PIN_PULSE);

    // Enable interrupts
    sei();                              
}

static void wdt_timer_start(uint8_t delay)
{
    wdt_reset();
    cli();
    MCUSR = 0x00;
    WDTCR |= _BV(WDCE) | _BV(WDE);
    switch (delay)
    {
        case SLEEP_64MS:
            WDTCR = _BV(WDIE) | _BV(WDP1);
            break;
        case SLEEP_128MS:
            WDTCR = _BV(WDIE) | _BV(WDP1) | _BV(WDP0);
            break;
        case SLEEP_1S:
            WDTCR = _BV(WDIE) | _BV(WDP0) | _BV(WDP2); // 1024ms
            break;
        case SLEEP_2S:
            WDTCR = _BV(WDIE) | _BV(WDP0) | _BV(WDP1) | _BV(WDP2); // 2048ms
            break;
    }
    sei();
}

static void wdt_stop(void)
{
    wdt_reset();
    cli();
    MCUSR = 0x00;
    WDTCR |= _BV(WDCE) | _BV(WDE);
    WDTCR = 0x00;
    sei();
}

static void start_sleep(void)
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    cli();                          // stop interrupts to ensure the BOD timed sequence executes as required
    sleep_enable();
    sleep_bod_disable();            // disable brown-out detection (good for 20-25µA)
    sei();                          // ensure interrupts enabled so we can wake up again
    sleep_cpu();                    // go to sleep
    sleep_disable();                // wake up here
}

// Handler for external interrupt on INT0 (PB2, pin 7)
ISR(INT0_vect)
{
    if (!_g_run_state.dial_pin_state)
    {
        // Disabling SF detection
        _g_run_state.flags = F_NONE;
        // A pulse just started
        _g_run_state.dialed_digit++;
    }
}

// Interrupt initiated by pin change on any enabled pin
ISR(PCINT0_vect)
{
}

// Handler for any unspecified 'bad' interrupts
ISR(BADISR_vect)
{
    // Do nothing, just wake up MCU
}

ISR(WDT_vect)
{
    _g_run_state.flags |= F_WDT_AWAKE;
}
