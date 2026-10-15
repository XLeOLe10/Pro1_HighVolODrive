# STM32G474 to STM32H750 port

The active G474 open-angle/current-feedback FOC program has been moved to the
STM32H750VBT6 project.

## Retained timing

- Cortex-M7/SYSCLK: 200 MHz (the existing H750 clock tree)
- TIM1 kernel clock: 200 MHz
- TIM1 center-aligned PWM: `200 MHz / (2 * (PSC + 1) * (ARR + 1))`
- PSC: 1, ARR: 2499, PWM frequency: 20 kHz
- Repetition counter: 1, control update interrupt: 20 kHz
- Dead time: DTG 100 at 200 MHz, approximately 500 ns (same time as DTG 50 at
  the 100 MHz G474 timer clock)

## Pin mapping

| Function | H750 pin/peripheral |
| --- | --- |
| U high PWM | PA8 / TIM1_CH1 |
| V high PWM | PA9 / TIM1_CH2 |
| W high PWM | PA10 / TIM1_CH3 |
| U low PWM | PE8 / TIM1_CH1N |
| V low PWM | PE10 / TIM1_CH2N |
| W low PWM | PB1 / TIM1_CH3N |
| Common gate-driver enable | PB10 / GPIO, active high |
| U current | PA0 / ADC1_INP16 injected rank 1 |
| V current | PA5 / ADC2_INP19 injected rank 1 |
| W current | PC0 / ADC1_INP10 injected rank 2 |
| DC bus voltage | PC3 / ADC3_INP1 injected rank 1 |
| USART1 TX | PB6 / USART1_TX AF7 |
| USART1 RX | PB7 / USART1_RX AF7 |

ADC1, ADC2 and ADC3 are triggered once per full PWM period from TIM1_TRGO2. U,
V and VBUS are simultaneous. W follows as ADC1 rank 2. ADC uses the 64 MHz
`per_ck` divided by four and a 64.5-cycle acquisition time; the effective
acquisition time is close to the original G474 setting. The original VBUS
divider ratio is retained. Because LQFP100 pin 18 is the dedicated `PC3_C`
analogue pad, its internal switch to digital PC3 is explicitly kept open.

USART1 is configured for 115200 baud, 8 data bits, no parity and one stop bit.
The original six-channel VOFA+ FireWater line is transmitted every 20 ms after
current-offset calibration completes.

For initial open-loop checkout, the motor uses a fixed normalized Q-axis voltage
of 0.08 and a 10 rpm/s mechanical-speed ramp to 100 rpm. The former V/F voltage
calculation has been removed. Rotor alignment is retained, and the transition
to the rotating Q-axis vector is phase-continuous.

## Power-stage enable sequence

PA5 was the active-high gate-enable output in the G474 program, but it is now
the V-current input. The H750 common gate-driver enable is PB10, active high.
PB10 is initialized low and remains low while the current offsets are measured.
After calibration, TIM1 CH1/2/3 and CH1N/2N/3N are enabled and connected to the
six PWM pads, then PB10 is driven high.

The previous PC11 diagnostic output remains unassigned. It is not required by
the active control law.
