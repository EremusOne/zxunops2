/*
Conversor de teclado ZX Spectrum a PS/2 
---------------------------------------

* Codigo base original por Quest
* Desarrollo inicial y primeros atajos de teclado por Neuro (@neurorulez)
* Mejoras, optimizaciones y nuevos atajos de teclado por @spark2k06
* Gracias a @desUBIkado por hacer de Betatester y proporcionar nuevas ideas para la versión alternativa.

*/

#include <stdio.h>
#include <avr/io.h>
#include <inttypes.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include "Config.h"
#include "Keymaps.h"

#define HI 1
#define LO 0
#define _IN 1
#define _OUT 0

#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))
#define CPU_16MHz       0x00

//Teclas Pulsadas en el ultimo pase
// Pulsada              -> 0b00000001 = 0x01
// Mantenida            -> 0b00000010 = 0x02
// Liberada             -> 0b00000100 = 0x04
// CapsShift            -> 0b00001000 = 0x08 (Este flag solo se tiene en cuenta en modos distintos al de ZX)
// SymbolShift          -> 0b00010000 = 0x10 (Este flag solo se tiene en cuenta en modos distintos al de ZX)

uint8_t matriz[ROWS][COLS];
uint8_t botones[NUMSW] = { 0,0 };
uint8_t opqa_cursors = 0;
uint8_t opqa_numbers = 0;
uint8_t fkbmode = 0;

unsigned char espera = 0;
unsigned char fnpulsada = 0;
unsigned char fnpulsando = 0;

KBMODE modo; //Modo teclado 0=ZX=NATIVO /Resto otros mapas
uint8_t cambiomodo = 0;
uint8_t funcion = 0;
uint8_t soltarteclas;
uint8_t cs_counter = 0, ss_counter = 0;

unsigned char   hostdata = 0;
unsigned char   hostdataAnt;
unsigned char   codeset = 2;
unsigned char   antighosting = 0;
unsigned char   kbescucha = 0;
uint32_t        timeout_escucha = 0;
uint16_t        typematic = 0;
uint16_t        typematicfirst = 0;
unsigned char   typematic_code = 0;
uint8_t         typematic_codeaux = 0;
uint8_t         kbcontrol = 0, kbalt = 0, kbshift = 0, kblwin = 0, kbaltgr = 0;
uint8_t         prevf0 = 0;

//Teclas Modificadoras (para teclado spectrum)
unsigned char CAPS_SHIFT = KEY_LSHIFT;  //Caps Shift   (NO necesita E0)
unsigned char SYMBOL_SHIFT = KEY_LCTRL; //Symbol Shift (NO necesita E0)

                                        //Caps Shift (CAPS_SHIFT)
#define CAPS_SHIFT_ROW 5  
#define CAPS_SHIFT_COL 0  

                                        //Symbol Shift (SYMBOL_SHIFT)
#define SYMBOL_SHIFT_ROW 7   
#define SYMBOL_SHIFT_COL 1   

                                        //SPACE (Escape)
#define SPACE_ROW 7 
#define SPACE_COL 0 

                                        //ENTER
#define ENTER_ROW 6 
#define ENTER_COL 0 

                                        //Row 1..5
#define N1_N5_ROW 0
                                        //Cols 1..5
#define N1_COL 0 //
#define N2_COL 1 //
#define N3_COL 2 //
#define N4_COL 3 //
#define N5_COL 4 //

                                        //Row 6..0
#define N6_N0_ROW 3
                                        //Cols 6..0
#define N6_COL 4 //
#define N7_COL 3 //
#define N8_COL 2 //
#define N9_COL 1 //
#define N0_COL 0 //

                                        //Row Q-T
#define Q_T_ROW 1
                                        //Cols Q-T
#define Q_COL 0 //
#define W_COL 1 //
#define E_COL 2 //
#define R_COL 3 //
#define T_COL 4 //

                                        //Row Y-P
#define Y_P_ROW 4
                                        //Cols Y-P
#define Y_COL 4 //
#define U_COL 3 //
#define I_COL 2 //
#define O_COL 1 //
#define P_COL 0 //

                                        //Row A-G
#define A_G_ROW 2
                                        //Cols A-G
#define A_COL 0 //
#define S_COL 1 //
#define D_COL 2 //
#define F_COL 3 //
#define G_COL 4 //

                                        //Row H-L
#define H_L_ROW 6
                                        //Cols H-L
#define H_COL 4 //
#define J_COL 3 //
#define K_COL 2 //
#define L_COL 1 //

                                        //Row Z-V
#define Z_V_ROW 5 //
                                        //Cols Z-V
#define Z_COL 1 //
#define X_COL 2 //
#define C_COL 3 //
#define V_COL 4 //

                                        //Row B-M
#define B_M_ROW 7
                                        //Cols B-M
#define B_COL 4 //
#define N_COL 3 //
#define M_COL 2 //
#define SP_COL 0 //

#ifndef atmega644
void leds_debug() {
    LED_ON;
    _delay_ms(60);
    LED_OFF;
    _delay_ms(30);
}
#endif

void sendPS2(unsigned char code);
void pulsa_y_suelta_teclas_modificadoras(uint8_t control, uint8_t alt, uint8_t shift, uint8_t altgr, uint8_t lwin);

void pinSet(uint8_t pin, uint8_t bcd, uint8_t stat) //stat 1 = in, stat 0 = out
{
    switch (bcd) {
#ifdef atmega644
    case PA:  if (stat) DDRA &= ~_BV(pin); else DDRA |= _BV(pin); break;
#endif
    case PB:  if (stat) DDRB &= ~_BV(pin); else DDRB |= _BV(pin); break;
    case PC:  if (stat) DDRC &= ~_BV(pin); else DDRC |= _BV(pin); break;
    case PD:  if (stat) DDRD &= ~_BV(pin); else DDRD |= _BV(pin); break;
    }
}

uint8_t pinStat(uint8_t pin, uint8_t bcd)
{
    switch (bcd) {
#ifdef atmega644
    case PA:  if (!(PINA & (1 << pin))) return 1; else return 0; break;
#endif
    case PB:  if (!(PINB & (1 << pin))) return 1; else return 0; break;
    case PC:  if (!(PINC & (1 << pin))) return 1; else return 0; break;
    case PD:  if (!(PIND & (1 << pin))) return 1; else return 0; break;
    }
    return 0;
}

void pinPut(uint8_t pin, uint8_t bcd, uint8_t stat) //stat 1 = HI, stat 0 = LO
{
    switch (bcd) {
#ifdef atmega644
    case PA:  if (!stat) PORTA &= ~_BV(pin); else PORTA |= _BV(pin); break;
#endif
    case PB:  if (!stat) PORTB &= ~_BV(pin); else PORTB |= _BV(pin); break;
    case PC:  if (!stat) PORTC &= ~_BV(pin); else PORTC |= _BV(pin); break;
    case PD:  if (!stat) PORTD &= ~_BV(pin); else PORTD |= _BV(pin); break;
    }
}

void ps2Mode(uint8_t pin, uint8_t mode)
{
    if (mode) PS2_DDR &= ~_BV(pin); //Hi-Entrada 
    else     PS2_DDR |= _BV(pin);  //Low-Salilda

}

void ps2Init()
{
    PS2_PORT &= ~_BV(PS2_DAT); //A 0
    PS2_PORT &= ~_BV(PS2_CLK); //A 0
    ps2Mode(PS2_DAT, HI);
    ps2Mode(PS2_CLK, HI);
}

uint8_t ps2Stat()
{
    if (!(PS2_PIN & (1 << PS2_CLK)))
        return 1;
    if (!(PS2_PIN & (1 << PS2_DAT)))
        return 1;
    return 0;
}

uint8_t checkState(uint16_t tramo)
{
    uint16_t cont = 0;
    while (cont++ < tramo) {
        if (!(PS2_PIN & (1 << PS2_DAT)))
            return 1;
        _delay_us(5);
    }

    return 0;
}

void _delay_us_4usteps(uint8_t us)
{
    while (0<us)
    {
        _delay_us(4);
        us -= 4;
    }
}

//En us, reloj y semireloj, para los flancos
//Uso normal: CK1 = 20, CK2 = 40 // Para codigo sin optimizar (x12) CK1 = 240, CK2 = 480.  //JOyPs2 CK1=15 CK2=30 //Nuevo JoyPs2 CK1=16 CK2=32 //Mio CK1=4 CK2=8
#define CK1 16
#define CK2 32
uint8_t CKm = 1;  //Multiplicador de CK1 y CK2

void sendPS2_aux(unsigned char code, unsigned char kbmodif)
{

    //Para continuar las líneas deben estar en alto
    while (ps2Stat());

    unsigned char parity = 1;
    uint8_t i = 0;

    //iniciamos transmisión
    ps2Mode(PS2_DAT, LO);
    _delay_us_4usteps(CK1*CKm);

    ps2Mode(PS2_CLK, LO); //bit de comienzo
    _delay_us_4usteps(CK2*CKm);
    ps2Mode(PS2_CLK, HI);
    _delay_us_4usteps(CK1*CKm);
    //enviamos datos
    for (i = 0; i < 8; ++i)
    {
        if (code & (1 << i))
        {
            ps2Mode(PS2_DAT, HI);
            parity = parity ^ 1;
        }
        else ps2Mode(PS2_DAT, LO);

        _delay_us_4usteps(CK1*CKm);
        ps2Mode(PS2_CLK, LO);
        _delay_us_4usteps(CK2*CKm);
        ps2Mode(PS2_CLK, HI);
        _delay_us_4usteps(CK1*CKm);
    }

    // Enviamos bit de paridad
    if (parity) ps2Mode(PS2_DAT, HI);
    else        ps2Mode(PS2_DAT, LO);

    _delay_us_4usteps(CK1*CKm);
    ps2Mode(PS2_CLK, LO);
    _delay_us_4usteps(CK2*CKm);
    ps2Mode(PS2_CLK, HI);
    _delay_us_4usteps(CK1*CKm);

    //Bit de parada
    ps2Mode(PS2_DAT, HI);
    _delay_us_4usteps(CK1*CKm);
    ps2Mode(PS2_CLK, LO);
    _delay_us_4usteps(CK2*CKm);
    ps2Mode(PS2_CLK, HI);
    _delay_us_4usteps(CK1*CKm);

    _delay_us(50);    //fin

    if (kbmodif)
    {
        if (codeset == 2)
        {
            if (kbcontrol && code == KEY_LCTRL) return;
            if (kbalt && code == KEY_LALT) return;
            if (kbshift && code == KEY_LSHIFT) return;
            if (kbaltgr && code == KEY_RALT) return;
            if (kblwin && code == KEY_LWIN) return;
            
            if (prevf0 && (code != 0xF0))
            {
                pulsa_y_suelta_teclas_modificadoras(1, 1, 1, 1, 1);
                prevf0 = 0;
            }
            else
            {
                prevf0 = (code == 0xF0);
            }
        }
        else
        {
            if (kbcontrol && code == KS1_LCTRL + KS1_RELEASE) return;
            if (kbalt && code == KS1_LALT + KS1_RELEASE) return;
            if (kbshift && code == KS1_LSHIFT + KS1_RELEASE) return;
            if (kbaltgr && code == KS1_RALT + KS1_RELEASE) return;
            if (kblwin && code == KS1_LWIN + KS1_RELEASE) return;

            if (code != 0xE0 && code > KS1_RELEASE)
            {
                pulsa_y_suelta_teclas_modificadoras(1, 1, 1, 1, 1);
            }
        }
    }
}

//envio de datos ps/2 simulando reloj con delays.
void sendPS2(unsigned char code)
{
    sendPS2_aux(code, 1);
}

int getPS2(unsigned char *ret) //Lectura de PS2 para acceso bidireccional
{
    unsigned char data = 0x00;
    unsigned char p = 0x01;
    uint8_t i = 0;

    // discard the start bit
    while ((PS2_PIN & (1 << PS2_DAT)));
    while (!(PS2_PIN & (1 << PS2_CLK)));

    // Bit de comienzo
    _delay_us_4usteps(CK1*CKm);
    ps2Mode(PS2_CLK, LO);
    _delay_us_4usteps(CK2*CKm);
    ps2Mode(PS2_CLK, HI);
    _delay_us_4usteps(CK1*CKm);

    // read each data bit
    for (i = 0; i<8; i++) {
        if ((PS2_PIN & (1 << PS2_DAT))) {
            data = data | (1 << i);
            p = p ^ 1;
        }
        _delay_us_4usteps(CK1*CKm);
        ps2Mode(PS2_CLK, LO);
        _delay_us_4usteps(CK2*CKm);
        ps2Mode(PS2_CLK, HI);
        _delay_us_4usteps(CK1*CKm);
    }

    // read the parity bit	
    if (((PS2_PIN & (1 << PS2_DAT)) != 0) != p) {
        return -1;
    }
    _delay_us_4usteps(CK1*CKm);
    ps2Mode(PS2_CLK, LO);
    _delay_us_4usteps(CK2*CKm);
    ps2Mode(PS2_CLK, HI);
    _delay_us_4usteps(CK1*CKm);

    // send 'ack' bit
    ps2Mode(PS2_DAT, LO);
    _delay_us_4usteps(CK1*CKm);
    ps2Mode(PS2_CLK, LO);
    _delay_us_4usteps(CK2*CKm);
    ps2Mode(PS2_CLK, HI);
    ps2Mode(PS2_DAT, HI);
    _delay_us(100);

    *ret = data;
    return 0;
}

void imprimeversion() //Imprime la fecha de la version en modos que no sean ZX ni PC
{
	int n;
	char pausa = 50;
	if (!modo)
	{
		sendPS2(0xF0); sendPS2(CAPS_SHIFT); matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] = 0;
		sendPS2(0xF0); sendPS2(SYMBOL_SHIFT); matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] = 0;
	}

	sendPS2(KEY_SPACE); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_SPACE);

	for (n = 0; n<8; n++)
	{
		if (n == 2 || n == 4) { _delay_ms(pausa); sendPS2(KEY_PUNTO); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_PUNTO); }
		_delay_ms(pausa);
		sendPS2(versionKeyCodes[version[n]]);
		_delay_ms(pausa);
		sendPS2(0xF0);
		sendPS2(versionKeyCodes[version[n]]);
		_delay_ms(pausa);    
   
	}

  sendPS2(KEY_SPACE); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_SPACE);
  sendPS2(KEY_R); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_R);
  _delay_ms(pausa);
  sendPS2(versionKeyCodes[rama[0]]);
  _delay_ms(pausa);
  sendPS2(0xF0);
  sendPS2(versionKeyCodes[rama[0]]);
  _delay_ms(pausa);   
 
	fnpulsada = 1;
	fnpulsando = 1;
}

void eepromsave() //Imprime ' .CFGFLASHED' y guarda en la EEPROM el modo actual
{
    int n;
    char pausa = 50;
    if (!modo)
    {
        sendPS2_aux(0xF0, 0); sendPS2_aux(CAPS_SHIFT, 0); matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] = 0;
        sendPS2_aux(0xF0, 0); sendPS2_aux(SYMBOL_SHIFT, 0); matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] = 0;
    }
    eeprom_write_byte((uint8_t*)5, (uint8_t)modo); // Guardamos el modo de teclado
    eeprom_write_byte((uint8_t*)6, (uint8_t)fkbmode); // Guardamos funcion de teclas modificadoras de ZX
    if (codeset == 2)
    {
        _delay_ms(pausa); sendPS2(KEY_SPACE); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_SPACE);
        _delay_ms(pausa); sendPS2(KEY_PUNTO); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_PUNTO);
        for (n = 0; n < 10; n++)
        {
            _delay_ms(pausa);
            sendPS2(eepromsavename[n]);
            _delay_ms(pausa);
            sendPS2(0xF0);
            sendPS2(eepromsavename[n]);
            _delay_ms(pausa);
        }
    }
    fnpulsada = 1;
    fnpulsando = 1;
}

void imprimecore(const uint8_t nomcore[]) //Imprime el nombre del core
{
    int n;
    char pausa = 100;

    if (!modo)
    {
        sendPS2_aux(0xF0, 0); sendPS2_aux(CAPS_SHIFT, 0); matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] = 0;
        sendPS2_aux(0xF0, 0); sendPS2_aux(SYMBOL_SHIFT, 0); matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] = 0;
    }

    _delay_ms(pausa); sendPS2(KEY_SPACE); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_SPACE);
    _delay_ms(pausa); sendPS2(KEY_PUNTO); _delay_ms(pausa); sendPS2(0xF0); sendPS2(KEY_PUNTO);
    for (n = 1; n<nomcore[0] + 1; n++)
    {
        _delay_ms(pausa);
        sendPS2(nomcore[n]);
        _delay_ms(pausa);
    sendPS2(0xF0);
        sendPS2(nomcore[n]);
        _delay_ms(pausa);
    }
    CKm = nomcore[nomcore[0] + 1]; //Valor de CKm en la configuracion de nomcore[]

}

//Inicializar Matriz
void matrixInit()
{
    uint8_t c, r;

#ifndef atmega644
    LED_CONFIG;
    LED_OFF;
#endif

#ifdef switches
    for (c = 0; c<NUMSW; c++)
    {
        pinSet(pinsSW[c], bcdSW[c], _IN);
        pinPut(pinsSW[c], bcdSW[c], HI);
    }
#endif

    for (c = 0; c<COLS; c++)
    {
        pinSet(pinsC[c], bcdC[c], _IN);
        pinPut(pinsC[c], bcdC[c], HI);
    }

    for (r = 0; r < ROWS; r++)
    {
        pinSet(pinsR[r], bcdR[r], _IN);
    }
}

KBMODE cambiarmodo2(KBMODE modokb)
{
    opqa_cursors = 0;
    opqa_numbers = 0;
    if (modokb == zx)  CKm = nomZX[nomZX[0] + 1];
    if (modokb == cpc) CKm = nomCPC[nomCPC[0] + 1];
    if (modokb == msx) CKm = nomMSX[nomMSX[0] + 1];
    if (modokb == c64) CKm = nomC64[nomC64[0] + 1];
    if (modokb == at8) CKm = nomAT8[nomAT8[0] + 1];
    if (modokb == bbc) CKm = nomBBC[nomBBC[0] + 1];
    if (modokb == electron) CKm = nomELECTRON[nomELECTRON[0] + 1];
    if (modokb == ap2) CKm = nomAP2[nomAP2[0] + 1];
    if (modokb == vic) CKm = nomVIC[nomVIC[0] + 1];
    if (modokb == ori) CKm = nomORI[nomORI[0] + 1];
    if (modokb == sam) CKm = nomSAM[nomSAM[0] + 1];
    if (modokb == jup) CKm = nomJUP[nomJUP[0] + 1];    
    if (modokb == c16) CKm = nomC16[nomC16[0] + 1];
    if (modokb == ht8) CKm = nomHT8[nomHT8[0] + 1];
    if (modokb == galak) CKm = nomGALAK[nomGALAK[0] + 1];
    if (modokb == multi) CKm = nomMULTI[nomMULTI[0] + 1];
    if (modokb == zx80) CKm = nomZX80[nomZX80[0] + 1];
    if (modokb == zx81) CKm = nomZX81[nomZX81[0] + 1];
    if (modokb == atom) CKm = nomATOM[nomATOM[0] + 1];
    if (modokb == cpet) CKm = nomCPET[nomCPET[0] + 1];
    if (modokb == eg2000) CKm = nomEG2000[nomEG2000[0] + 1];
    if (modokb == lynx) CKm = nomLYNX[nomLYNX[0] + 1];
    if (modokb == enterp) CKm = nomenterp[nomenterp[0] + 1];

    if (modokb == pc) { CKm = nomPC[nomPC[0] + 1]; kbescucha = 1; timeout_escucha = 0; codeset = 2; } // Iniciamos la escucha para que se pueda cambiar al core de PC/XT.
    
    return modokb;
}

void ejecutarfuncion(unsigned char code)
{
    funcion = 0; //para salir del bucle de la funcion
    switch (code)
    {
        case KEY_U:
            cambiomodo = 1;
            _delay_ms(250);
            break;

        case KEY_C:
            if (modo == at8 || modo == jup || modo == c64)
            {
                opqa_cursors = 0;
                opqa_numbers = !opqa_numbers; // OPQA -> Bloq Num (Activacion / Desactivacion)

                fnpulsada = 1;
                fnpulsando = 1;
            }
            else
            {
                opqa_numbers = 0;
                opqa_cursors = !opqa_cursors; // OPQA -> Cursores (Activacion / Desactivacion)

                fnpulsada = 1;
                fnpulsando = 1;
            }
            break;

        case KEY_D:
            if (!modo) // Funcion valida solo para el modo ZX
            {
                fkbmode = !fkbmode; // Teclas modificadoras (Activacion / Desactivacion)				
                sendPS2_aux(0xF0, 0); sendPS2_aux(CAPS_SHIFT, 0); matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] = 0;
                sendPS2_aux(0xF0, 0); sendPS2_aux(SYMBOL_SHIFT, 0); matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] = 0;

                fnpulsada = 1;
                fnpulsando = 1;
            }
            break;

        case KEY_H:
            if (modo == at8)
            {
                // F8 + F10 para Atari
                sendPS2(KEY_F8);
                _delay_ms(50);
                sendPS2(KEY_F10);
                _delay_ms(50);

                sendPS2(0xF0);
                sendPS2(KEY_F10);
                _delay_ms(1000);
                sendPS2(0xF0);
                sendPS2(KEY_F8);
                _delay_ms(50);

                fnpulsada = 1;
                fnpulsando = 1;
            }
            break;
        case KEY_O:
            if (modo == bbc)
            {
                // F12 y seguido shift pulsado con delay de 1 segundo antes de soltar para BBC
                pulsafn(Y_P_ROW, O_COL, KEY_F12, 0, 0, 0, 0, 0, 0);
                sendPS2(KEY_LSHIFT);
                _delay_ms(1000);
                sendPS2(0xF0); sendPS2(KEY_LSHIFT);

                fnpulsada = 1;
                fnpulsando = 1;
            }
            if (modo == msx) // Hard Reset
            {
                // Control+Alt+F12
                pulsafn(Y_P_ROW, Y_COL, codeset == 1 ? KS1_F12 : KEY_F12, 0, 0, 1, 1, 0, 0);
            }

            break;

        case KEY_V:
            if (codeset == 2) 
            imprimeversion();
            break;

        case KEY_X:	
            // Guarda en la EEPROM el modo actual de teclado
            eepromsave();																	
            break;

        case KEY_B:	
            // ZXUNO Hard Reset (Control+Alt+Backsp)
            pulsafn(B_M_ROW, B_COL, codeset == 1 ? KS1_BACKSP : KEY_BACKSP, 0, 0, 1, 1, 0, 0);	
            break;

        case KEY_N:	
            // ZXUNO Soft Reset (Control+Alt+Supr)
            pulsafn(B_M_ROW, N_COL, codeset == 1 ? KS1_DELETE : KEY_DELETE, 1, 0, 1, 1, 0, 0);
            if (modo == pc)
            {
                kbescucha = 1; timeout_escucha = 0; codeset = 2; // Iniciamos la escucha para que se pueda cambiar al core de PC/XT.
            }
            break;

        case KEY_Y: // NMI
            if (modo == zx || modo == cpc || modo == jup || modo == sam || modo == zx80 || modo == zx81)
            {
                // Control+Alt+F5
                pulsafn(Y_P_ROW, Y_COL, codeset == 1 ? KS1_F5 : KEY_F5, 0, 0, 1, 1, 0, 0);
            }
            break;

        case KEY_G: // VGA-RGB
            if (modo == zx || modo == at8 || modo == bbc || modo == vic || modo == ori || modo == ht8)
            {
                pulsafn(A_G_ROW, G_COL, codeset == 1 ? KS1_SCRLCK : KEY_SCRLCK, 0, 0, 1, 1, 0, 0);
            }
            if (modo == msx)
            {
                pulsaimprpant(A_G_ROW, G_COL);
            }
            break;

        default:
            fnpulsada = 1;
            fnpulsando = 1;
            break;
    }
}

KBMODE cambiarmodo(KBMODE modokb)
{
    KBMODE auxmodo = modo;
    opqa_cursors = 0;
    opqa_numbers = 0;
    muestramodo:
    if (modokb == zx)  imprimecore(nomZX);
    if (modokb == cpc) imprimecore(nomCPC);
    if (modokb == msx) imprimecore(nomMSX);
    if (modokb == c64) imprimecore(nomC64);
    if (modokb == at8) imprimecore(nomAT8);
    if (modokb == bbc) imprimecore(nomBBC);
    if (modokb == electron) imprimecore(nomELECTRON);
    if (modokb == ap2) imprimecore(nomAP2);
    if (modokb == vic) imprimecore(nomVIC);
    if (modokb == ori) imprimecore(nomORI);
    if (modokb == sam) imprimecore(nomSAM);
    if (modokb == jup) imprimecore(nomJUP);    
    if (modokb == c16) imprimecore(nomC16);
    if (modokb == ht8) imprimecore(nomHT8);
    if (modokb == galak) imprimecore(nomGALAK);
    if (modokb == multi) imprimecore(nomMULTI);
    if (modokb == zx80) imprimecore(nomZX80);
    if (modokb == zx81) imprimecore(nomZX81);
    if (modokb == atom) imprimecore(nomATOM);
    if (modokb == cpet) imprimecore(nomCPET);
    if (modokb == eg2000) imprimecore(nomEG2000);
    if (modokb == lynx) imprimecore(nomLYNX);
    if (modokb == enterp) imprimecore(nomenterp);
    
    if (modokb == pc) { kbescucha = 1; timeout_escucha = 0; codeset = 2; imprimecore(nomPC); } // Iniciamos la escucha para que se pueda cambiar al core de PC/XT.

    if (modokb == -1)
    {
        modokb = auxmodo; // Si no se trata de un modo conocido, mantenemos el anterior y lo imprimimos por pantalla.
        goto muestramodo;
    }
    
    if (modokb<MAXKB) codeset = 2;
    fnpulsada = 1;
    fnpulsando = 1;
    cambiomodo = 0; //para salir del bucle del cambiomodo
    return modokb;

}

void is_fn(unsigned char row, unsigned char col)
{
    pulsa_y_suelta_teclas_modificadoras(1, 1, 1, 1, 1);
    matriz[row][col] = 0;
}

void pulsapausa(unsigned char row, unsigned char col)
{
    sendPS2_aux(0xE1, 0); sendPS2_aux(0x14, 0); sendPS2_aux(0x77, 0); sendPS2_aux(0xE1, 0);
    sendPS2_aux(0xF0, 0); sendPS2_aux(0x14, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(0x77, 0);
    _delay_ms(100);
}

void pulsaimprpant(unsigned char row, unsigned char col)
{
    sendPS2_aux(0xE0, 0); sendPS2_aux(0x12, 0); sendPS2_aux(0xE0, 0); sendPS2_aux(0x7C, 0);
    _delay_ms(100);
    sendPS2_aux(0xE0, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(0x12, 0); sendPS2_aux(0xE0, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(0x7C, 0);
}

void pulsa_alt_code_set1(unsigned char d1, unsigned char d2, unsigned char d3)
{
    if (d1 + d2 + d3 == 0) return;
    sendPS2_aux(KS1_LALT, 0);
    if (d1 != 0) { sendPS2_aux(d1, 0); sendPS2_aux(d1 + KS1_RELEASE, 0); }
    if (d2 != 0) { sendPS2_aux(d2, 0); sendPS2_aux(d2 + KS1_RELEASE, 0); }
    if (d3 != 0) { sendPS2_aux(d3, 0); sendPS2_aux(d3 + KS1_RELEASE, 0); }

sendPS2_aux(KS1_LALT + KS1_RELEASE, 0); 
}

void pulsa_alt_code_set2(unsigned char d1, unsigned char d2, unsigned char d3)
{
    if (d1 + d2 + d3 == 0) return;
    sendPS2_aux(KEY_LALT, 0);
    if (d1 != 0) { sendPS2_aux(d1, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(d1, 0); }
    if (d2 != 0) { sendPS2_aux(d2, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(d2, 0); }
    if (d3 != 0) { sendPS2_aux(d3, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(d3, 0); }

    sendPS2_aux(0xF0, 0); sendPS2_aux(KEY_LALT, 0); 
}

void pulsafn(unsigned char row, unsigned char col, unsigned char key, unsigned char key_E0, unsigned char shift, unsigned char ctrl, unsigned char alt, unsigned char altgr, unsigned char lwin)
{
    if (espera) { _delay_us(5); espera = 0; }
    if (shift) { if (codeset == 2) { sendPS2(KEY_LSHIFT); espera++; } else { sendPS2(KS1_LSHIFT); espera++; } }//El Shift no necesita E0
    if (ctrl) { if (codeset == 2) { sendPS2(KEY_LCTRL);  espera++; } else { sendPS2(KS1_LCTRL);  espera++; } }// Pulsamos siempre el CTRL Izquierdo para mayor compatibilidad con todos los cores.
    if (alt) { if (codeset == 2) { sendPS2(KEY_LALT);   espera++; } else { sendPS2(KS1_LALT);   espera++; } }// Alt izdo
    if (altgr) { if (codeset == 2) { sendPS2(0xE0); sendPS2(KEY_RALT);   espera++; } else { sendPS2(0xE0); sendPS2(KS1_RALT);   espera++; } }// AltGr
    if (lwin) { if (codeset == 2) { sendPS2(0xE0); sendPS2(KEY_LWIN);   espera++; } else { sendPS2(0xE0); sendPS2(KS1_LWIN);   espera++; } }// LWin
    if (espera) { _delay_us(5); espera = 0; }
    if (key_E0) { sendPS2(0xE0); } //La tecla requiere modo E0 del PS2
    sendPS2(key);
    _delay_ms(100);
    if (key_E0) { sendPS2(0xE0); }
    if (codeset == 2) sendPS2(0xF0);
    if (codeset == 2) sendPS2(key);  else sendPS2(key + KS1_RELEASE);
    matriz[row][col] = 0;
    if (shift) { if (codeset == 2) { sendPS2(0xF0); sendPS2(KEY_LSHIFT); } else { sendPS2(KS1_LSHIFT + KS1_RELEASE); } }
    if (ctrl) { if (codeset == 2) { sendPS2(0xF0); sendPS2(KEY_RCTRL); } else { sendPS2(KS1_RCTRL + KS1_RELEASE); } }
    if (alt) { if (codeset == 2) { sendPS2(0xF0); sendPS2(KEY_LALT); } else { sendPS2(KS1_RALT + KS1_RELEASE); } }
    if (altgr) { if (codeset == 2) { sendPS2(0xE0); sendPS2(0xF0); sendPS2(KEY_LALT); } else { sendPS2(0xE0); sendPS2(KS1_RALT + KS1_RELEASE); } }
    if (lwin) { if (codeset == 2) { sendPS2(0xE0); sendPS2(0xF0); sendPS2(KEY_LALT); } else { sendPS2(0xE0); sendPS2(KS1_RALT + KS1_RELEASE); } }
    //_delay_us(5);
    _delay_ms(100);
    pulsa_y_suelta_teclas_modificadoras(1, 1, 1, 1, 1);
    matriz[row][col] = 0;
}

void traducekey(uint8_t &key, uint8_t &keymodif, KBMODE modokb) // con esta funcion ahorramos muchas matrices de mapas y por tanto memoria dinamica del AVR
{
    // 0x1 -> LShift
    // 0x2 -> LCTrl
    // 0x4 -> LAlt
    // 0x8 -> RAlt (AltGr)
    // 0x10 -> RCTrl
    // 0x20 -> Requiere 0xE0
    // 0x40 -> AltCode
    // 0x80 -> Requiere SHIFT + ENTER de ZX81
    // 0x80 -> Requiere Symbol de SAM Coupe 

    if (modokb == pc && codeset == 1) return; // pcxt tiene su propio mapa con scancodes distintos

    // traduccion de key segun el core
    if (key != 0)
    {
        switch (modokb)
        {

#ifdef symbols_cpc
        case cpc:
        keymodif = 
        key == KEY_A ? 2 : 
        key == KEY_2 ? 0 : 
        key == KEY_D ? 0 : 
        key == KEY_Y ? 0 : 
        key == KEY_U ? 0 : 
        key == KEY_O ? 0 : 
        key == KEY_V ? 0 :
        key == KEY_Z ? 0 : 
        key == KEY_H ? 0 : 
        key == KEY_J ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 : 
        1;
        key =
        key == KEY_2 ? KEY_ACORCHE : key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA : key == KEY_G ? KEY_BKSLASH :
        key == KEY_F ? KEY_CCORCHE : key == KEY_D ? KEY_LESS : key == KEY_S ? KEY_ACORCHE : key == KEY_A ? KEY_2 :
        key == KEY_Y ? KEY_CCORCHE : key == KEY_U ? KEY_BKSLASH : key == KEY_O ? KEY_COMILLA : key == KEY_P ? KEY_2 :
        key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : key == KEY_X ? KEY_IGUAL : key == KEY_Z ? KEY_PTOCOMA :
        key == KEY_H ? KEY_IGUAL : key == KEY_J ? KEY_MENOS : key == KEY_K ? KEY_COMILLA : key == KEY_L ? KEY_MENOS :
        key == KEY_B ? KEY_PTOCOMA : key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9 || key == KEY_0) ? key : 0;
        break;
#endif

#ifdef symbols_msx
        case msx:
        keymodif = 
        key == KEY_W ? 1 :
        key == KEY_E ? 9 :
        key == KEY_I ? 1 :
        key == KEY_1 ? 1 : 
        key == KEY_2 ? 8 : 
        key == KEY_3 ? 8 : 
        key == KEY_4 ? 1 : 
        key == KEY_5 ? 1 : 
        key == KEY_6 ? 1 :
        key == KEY_8 ? 1 : 
        key == KEY_9 ? 1 : 
        key == KEY_0 ? 1 : 
        key == KEY_T ? 1 : 
        key == KEY_Y ? 8 :
        key == KEY_U ? 8 :
        key == KEY_O ? 1 : 
        key == KEY_P ? 1 : 
        key == KEY_A ? 8 :
        key == KEY_S ? 5 :
        key == KEY_D ? 8 :
        key == KEY_F ? 8 :
        key == KEY_G ? 8 :
        key == KEY_H ? 1 :
        key == KEY_L ? 1 :
        key == KEY_Z ? 1 :
        key == KEY_X ? 9 :
        key == KEY_C ? 1 :
        key == KEY_V ? 1 :
        key == KEY_B ? 1 :
        0;   
        key =
        key == KEY_Q ? KEY_TLD : key == KEY_W ? KEY_TLD : key == KEY_E ? KEY_1 : key == KEY_I ? KEY_IGUAL :
        key == KEY_7 ? KEY_MENOS : key == KEY_0 ? KEY_SLASH : key == KEY_R ? KEY_LESS : key == KEY_T ? KEY_LESS :
        key == KEY_Y ? KEY_ACORCHE : key == KEY_U ? KEY_CCORCHE : key == KEY_O ? KEY_COMA : key == KEY_P ? KEY_2 :
        key == KEY_A ? KEY_4 : key == KEY_S ? KEY_1 : key == KEY_D ? KEY_TLD : key == KEY_F ? KEY_COMILLA :
        key == KEY_G ? KEY_BKSLASH : key == KEY_H ? KEY_ACORCHE : key == KEY_J ? KEY_SLASH : key == KEY_K ? KEY_CCORCHE :
        key == KEY_L ? KEY_0 : key == KEY_Z ? KEY_PUNTO : key == KEY_X ? KEY_4 : key == KEY_C ? KEY_MENOS : key == KEY_3 ? key :
        key == KEY_V ? KEY_7 : key == KEY_B ? KEY_CCORCHE : key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO :
        (key == KEY_1 || key == KEY_2 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_c64        
        case c64:
        keymodif = 
        key == KEY_0 ? 4 : 
        key == KEY_2 ? 0 : 
        key == KEY_O ? 0 : 
        key == KEY_V ? 0 : 
        key == KEY_Z ? 0 : 
        key == KEY_H ? 0 :
        key == KEY_J ? 0 : 
        key == KEY_K ? 0 :
        key == KEY_L ? 0 : 
        key == KEY_B ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 : 
        key == KEY_X ? 0 : 
        1;   
        key =
        key == KEY_0 ? KEY_ACORCHE :
        key == KEY_2 ? KEY_ACORCHE : key == KEY_R ? KEY_COMA : key == KEY_T ? KEY_PUNTO : key == KEY_S ? KEY_MENOS :
        key == KEY_Y ? KEY_PTOCOMA : key == KEY_U ? KEY_COMILLA : key == KEY_O ? KEY_COMILLA : key == KEY_P ? KEY_2 :
        key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : key == KEY_Z ? KEY_PTOCOMA : key == KEY_H ? KEY_BKSLASH :
        key == KEY_J ? KEY_MENOS : key == KEY_K ? KEY_F10 : key == KEY_L ? KEY_IGUAL : key == KEY_B ? KEY_CCORCHE :
        key == KEY_M ? KEY_PUNTO : key == KEY_N ? KEY_COMA : key == KEY_X ? KEY_F9 :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_at8
        case at8:
        keymodif =         
        key == KEY_T ? 0 : 
        key == KEY_R ? 0 : 
        key == KEY_W ? 0 : 
        key == KEY_Q ? 0 : 
        key == KEY_O ? 0 :
        key == KEY_V ? 0 : 
        key == KEY_J ? 0 :
        key == KEY_K ? 0 : 
        key == KEY_L ? 0 : 
        key == KEY_B ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 : 
        1;  
        key =
        key == KEY_8 ? KEY_9 : key == KEY_9 ? KEY_0 : key == KEY_0 ? KEY_ACORCHE :
        key == KEY_2 ? KEY_8 : key == KEY_T ? KEY_IGUAL : key == KEY_R ? KEY_MENOS : key == KEY_W ? KEY_F12 :
        key == KEY_Q ? KEY_F11 : key == KEY_D ? KEY_COMILLA : key == KEY_S ? KEY_CCORCHE : key == KEY_Y ? KEY_COMA :
        key == KEY_U ? KEY_PUNTO : key == KEY_O ? KEY_PTOCOMA : key == KEY_P ? KEY_2 : key == KEY_V ? KEY_SLASH :
        key == KEY_C ? KEY_SLASH : key == KEY_Z ? KEY_PTOCOMA : key == KEY_H ? KEY_BKSLASH : key == KEY_J ? KEY_ACORCHE :
        key == KEY_K ? KEY_COMILLA : key == KEY_L ? KEY_CCORCHE : key == KEY_B ? KEY_BKSLASH : key == KEY_N ? KEY_COMA :
        key == KEY_M ? KEY_PUNTO :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7) ? key : 0;
        break;
#endif

#ifdef symbols_bbc
        case bbc:        
        keymodif =         
        key == KEY_D ? 0 :
        key == KEY_H ? 0 :
        key == KEY_O ? 0 :
        key == KEY_V ? 0 : 
        key == KEY_Y ? 0 : 
        key == KEY_U ? 0 : 
        key == KEY_Z ? 0 :
        key == KEY_J ? 0 : 
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :         
        key == KEY_0 ? 0 : 
        1; 
        key =
        key == KEY_2 ? KEY_TLD : key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA : key == KEY_0 ? KEY_BKSLASH :
        key == KEY_A ? KEY_IGUAL : key == KEY_S ? KEY_LESS : key == KEY_D ? KEY_LESS : key == KEY_A ? KEY_IGUAL : key == KEY_H ? KEY_IGUAL :
        key == KEY_O ? KEY_PTOCOMA : key == KEY_P ? KEY_2 : key == KEY_V ? KEY_SLASH : key == KEY_Y ? KEY_ACORCHE :
        key == KEY_C ? KEY_SLASH : key == KEY_X ? KEY_BKSLASH : key == KEY_Z ? KEY_COMILLA : key == KEY_U ? KEY_CCORCHE :
        key == KEY_J ? KEY_MENOS : key == KEY_K ? KEY_PTOCOMA : key == KEY_L ? KEY_MENOS : key == KEY_F ? KEY_ACORCHE :
        key == KEY_B ? KEY_COMILLA : key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO : key == KEY_G ? KEY_CCORCHE :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_electron
        case electron:
        keymodif =        
        key == KEY_O ? 0 : 
        key == KEY_V ? 0 : 
        key == KEY_Z ? 0 :
        key == KEY_J ? 0 : 
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :         
        key == KEY_F ? 2 :
        key == KEY_G ? 2 :
        key == KEY_D ? 2 :
        key == KEY_A ? 2 :
        key == KEY_U ? 2 :
        1; 
        key =
        key == KEY_2 ? KEY_0 : key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA : key == KEY_D ? KEYPAD_6 :
        key == KEY_A ? KEYPAD_4 : key == KEY_0 ? KEYPAD_2 : key == KEY_H ? KEYPAD_4 : key == KEY_S ? KEYPAD_6 : 
        key == KEY_Y ? KEYPAD_1 : key == KEY_U ? KEYPAD_1 : key == KEY_O ? KEY_PTOCOMA : key == KEY_P ? KEY_2 : key == KEY_G ? KEY_CCORCHE :
        key == KEY_F ? KEY_ACORCHE : key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : key == KEY_X ? KEY_ACORCHE : key == KEY_Z ? KEY_COMILLA :
        key == KEY_J ? KEY_MENOS : key == KEY_K ? KEY_PTOCOMA : key == KEY_L ? KEY_MENOS : key == KEY_B ? KEY_COMILLA :
        key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_ap2
        case ap2:
        keymodif = 
        key == KEY_7 ? 0 : 
        key == KEY_Y ? 0 : 
        key == KEY_U ? 0 : 
        key == KEY_O ? 0 : 
        key == KEY_V ? 0 :
        key == KEY_J ? 0 : 
        key == KEY_L ? 0 :
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 :  
        1; 
        key =
        key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA : key == KEY_6 ? KEY_7 : key == KEY_7 ? KEY_COMILLA :
        key == KEY_8 ? KEY_9 : key == KEY_9 ? KEY_0 : key == KEY_0 ? KEY_MENOS : key == KEY_Y ? KEY_ACORCHE :
        key == KEY_U ? KEY_CCORCHE : key == KEY_O ? KEY_PTOCOMA : key == KEY_P ? KEY_COMILLA : key == KEY_V ? KEY_SLASH :
        key == KEY_C ? KEY_SLASH : key == KEY_Z ? KEY_PTOCOMA : key == KEY_H ? KEY_6 : key == KEY_J ? KEY_MENOS :
        key == KEY_K ? KEY_IGUAL : key == KEY_L ? KEY_IGUAL : key == KEY_B ? KEY_8 : key == KEY_N ? KEY_COMA : 
        key == KEY_M ? KEY_PUNTO : (key == KEY_1 || key == KEY_2 || key == KEY_3 || key == KEY_4 || key == KEY_5) ? key : 0;
        break;
#endif

#ifdef symbols_vic
        case vic:
        keymodif =
        key == KEY_H ? 0x20 :
        key == KEY_X ? 0x20 :        
        key == KEY_0 ? 2 :
        key == KEY_2 ? 0 : 
        key == KEY_O ? 0 : 
        key == KEY_V ? 0 : 
        key == KEY_Z ? 0 : 
        key == KEY_J ? 0 :
        key == KEY_K ? 0 : 
        key == KEY_L ? 0 :
        key == KEY_B ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 :  
        1; 
        key =
        key == KEY_H ? KEY_DELETE : key == KEY_X ? KEY_INS : key == KEY_D ? KEY_M : key == KEY_0 ? KEY_ACORCHE :
        key == KEY_2 ? KEY_ACORCHE : key == KEY_R ? KEY_COMA : key == KEY_T ? KEY_PUNTO : key == KEY_S ? KEY_IGUAL :
        key == KEY_Y ? KEY_PTOCOMA : key == KEY_U ? KEY_COMILLA : key == KEY_O ? KEY_COMILLA : key == KEY_P ? KEY_2 :
        key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : key == KEY_Z ? KEY_PTOCOMA : key == KEY_J ? KEY_IGUAL :
        key == KEY_K ? KEY_MENOS : key == KEY_L ? KEY_BKSLASH : key == KEY_B ? KEY_CCORCHE : key == KEY_M ? KEY_PUNTO :
        key == KEY_N ? KEY_COMA : key == KEY_0 ? KEY_ACORCHE :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_pc

    case pc: // scancodes set 2
        keymodif = 
        key == KEY_A ? 0x40 :
        key == KEY_X ? 0x40 :
        key == KEY_2 ? 8 : 
        key == KEY_3 ? 8 :
        key == KEY_Q ? 0 : 
        key == KEY_E ? 0 : 
        key == KEY_R ? 0 :        
        key == KEY_S ? 8 :
        key == KEY_D ? 8 : 
        key == KEY_F ? 8 : 
        key == KEY_G ? 8 :
        key == KEY_7 ? 0 :        
        key == KEY_U ? 8 : 
        key == KEY_Y ? 8 :        
        key == KEY_K ? 0 : 
        key == KEY_J ? 0 :
        key == KEY_M ? 0 : 
        key == KEY_N ? 0 : 
        1;  
        key = 
        key == KEY_A ? 126 : key == KEY_X ? 156 :
        key == KEY_Q ? KEY_TLD : key == KEY_W ? KEY_TLD : key == KEY_E ? KEY_IGUAL : key == KEY_R ? KEY_LESS : key == KEY_T ? KEY_LESS : 
        key == KEY_S ? KEY_1 : key == KEY_D ? KEY_TLD : key == KEY_F ? KEY_COMILLA : key == KEY_G ? KEY_BKSLASH : key == KEY_0 ? KEY_SLASH : 
        key == KEY_7 ? KEY_MENOS : key == KEY_P ? KEY_2 : key == KEY_O ? KEY_COMA : key == KEY_I ? KEY_IGUAL : key == KEY_U ? KEY_CCORCHE : 
        key == KEY_Y ? KEY_ACORCHE : key == KEY_Z ? KEY_PUNTO : key == KEY_C ? KEY_MENOS : key == KEY_V ? KEY_7 : key == KEY_L ? KEY_0 : 
        key == KEY_K ? KEY_CCORCHE : key == KEY_J ? KEY_SLASH : key == KEY_H ? KEY_ACORCHE : key == KEY_M ? KEY_PUNTO : 
        key == KEY_N ? KEY_COMA : key == KEY_B ? KEY_CCORCHE : 
        (key == KEY_2 || key == KEY_3 || key == KEY_6 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_ori
    case ori:
        keymodif =                
        key == KEY_D ? 0 : 
        key == KEY_7 ? 0 : 
        key == KEY_Y ? 0 : 
        key == KEY_U ? 0 : 
        key == KEY_O ? 0 :
        key == KEY_V ? 0 : 
        key == KEY_J ? 0 :
        key == KEY_L ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 : 
        1; 
        key =                
        key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA : key == KEY_G ? KEY_CCORCHE : key == KEY_F ? KEY_ACORCHE :
        key == KEY_D ? KEY_BKSLASH : key == KEY_S ? KEY_BKSLASH : key == KEY_6 ? KEY_7 : key == KEY_7 ? KEY_COMILLA :
        key == KEY_8 ? KEY_9 : key == KEY_9 ? KEY_0 : key == KEY_Y ? KEY_ACORCHE : key == KEY_U ? KEY_CCORCHE :
        key == KEY_O ? KEY_PTOCOMA : key == KEY_P ? KEY_COMILLA : key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH :
        key == KEY_X ? KEY_MENOS : key == KEY_Z ? KEY_PTOCOMA : key == KEY_H ? KEY_6 : key == KEY_J ? KEY_MENOS :
        key == KEY_K ? KEY_IGUAL : key == KEY_L ? KEY_IGUAL : key == KEY_B ? KEY_8 : key == KEY_N ? KEY_COMA :
        key == KEY_M ? KEY_PUNTO : (key == KEY_1 || key == KEY_2 || key == KEY_3 || key == KEY_4 || key == KEY_5) ? key : 0;
        break;
#endif

#ifdef symbols_sam
        case sam:
        keymodif = 
        key == KEY_R ? 0 : 
        key == KEY_G ? 0 : 
        key == KEY_F ? 0 : 
        key == KEY_A ? 0 : 
        key == KEY_7 ? 0 :
        key == KEY_X ? 0 : 
        key == KEY_J ? 0 :
        key == KEY_K ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 : 
        key == KEY_S ? 0x80 : 
        key == KEY_Y ? 0x80 : 
        key == KEY_U ? 0x80 : 
        key == KEY_D ? 0x80 : 
        1; 
        key =
        key == KEY_Q ? KEY_BKSLASH : key == KEY_T ? KEY_LESS : key == KEY_R ? KEY_LESS : key == KEY_G ? KEY_BKSLASH :
        key == KEY_F ? KEY_COMILLA : key == KEY_A ? KEY_PTOCOMA : key == KEY_7 ? KEY_MENOS : key == KEY_0 ? KEY_SLASH :
        key == KEY_O ? KEY_COMA : key == KEY_P ? KEY_2 : key == KEY_V ? KEY_7 : key == KEY_C ? KEY_MENOS :
        key == KEY_X ? KEY_ACORCHE : key == KEY_Z ? KEY_PUNTO : key == KEY_H ? KEY_ACORCHE : key == KEY_J ? KEY_SLASH :
        key == KEY_K ? KEY_CCORCHE : key == KEY_L ? KEY_0 : key == KEY_B ? KEY_CCORCHE : key == KEY_N ? KEY_COMA :
        key == KEY_M ? KEY_PUNTO : key == KEY_S ? KEY_9 : key == KEY_Y ? KEY_R : key == KEY_U ? KEY_T : key == KEY_D ? KEY_LALT :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_jup
        case jup:
        keymodif = 
        key == KEY_U ? 4 :
        key == KEY_S ? 4 :
        key == KEY_A ? 0x10 :
        key == KEY_G ? 0x10 : 
        key == KEY_Q ? 0 : 
        key == KEY_2 ? 0x10 : 
        key == KEY_R ? 0 : 
        key == KEY_D ? 0 : 
        key == KEY_7 ? 0 : 
        key == KEY_X ? 0 :
        key == KEY_H ? 0 : 
        key == KEY_J ? 0 :
        key == KEY_K ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 :  
        1; 
        key =
        key == KEY_U ? KEY_CCORCHE : key == KEY_S ? KEY_1 : key == KEY_Q ? KEY_BKSLASH :
        key == KEY_T ? KEY_LESS : key == KEY_R ? KEY_LESS : key == KEY_F ? KEY_COMILLA :
        key == KEY_D ? KEY_TLD : key == KEY_7 ? KEY_MENOS : key == KEY_0 ? KEY_SLASH : key == KEY_Y ? KEY_ACORCHE :
        key == KEY_O ? KEY_COMA : key == KEY_P ? KEY_2 : key == KEY_V ? KEY_7 : key == KEY_C ? KEY_MENOS : key == KEY_X ? KEY_COMILLA :
        key == KEY_Z ? KEY_PUNTO : key == KEY_H ? KEY_ACORCHE : key == KEY_J ? KEY_SLASH : key == KEY_K ? KEY_CCORCHE :
        key == KEY_L ? KEY_0 : key == KEY_B ? KEY_CCORCHE : key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO :
        (key == KEY_1 || key == KEY_2 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_8 || key == KEY_9 || key == KEY_G || key == KEY_A) ? key : 0;
        break;
#endif

#ifdef symbols_zx81
        case zx81:
        keymodif = 
        key == KEY_M ? 0 :
        key == KEY_S ? 0x80 :
        key == KEY_I ? 0x80 :
        1;
        key = 
        key == KEY_G ? KEY_3 : key == KEY_4 ? KEY_U : key == KEY_S ? KEY_N : key == KEY_I ? KEY_C : 
        key == KEY_Y ? KEY_2 : key == KEY_F ? KEY_4 : key == KEY_D ? KEY_E : key == KEY_U ? KEY_W :
        key == KEY_Q ? KEY_R : key == KEY_W ? KEY_T : key == KEY_E ? KEY_Y : key == KEY_8 ? KEY_I :
        key == KEY_9 ? KEY_O : key == KEY_O ? KEY_X : key == KEY_R ? KEY_N : key == KEY_T ? KEY_M :
        key == KEY_N ? KEY_PUNTO : key == KEY_M ? KEY_PUNTO : key == KEY_X ? KEY_SPACE :
        (key == KEY_P || key == KEY_J || key == KEY_K || key == KEY_L || key == KEY_Z || 
        key == KEY_C || key == KEY_V || key == KEY_B || key == KEY_H) ? key : 0;
        
        break;
#endif

#ifdef symbols_c16
        case c16:
        keymodif =         
        key == KEY_2 ? 0 : 
        key == KEY_O ? 0 : 
        key == KEY_V ? 0 : 
        key == KEY_Z ? 0 :         
        key == KEY_J ? 0 : 
        key == KEY_K ? 0 :
        key == KEY_L ? 0 : 
        key == KEY_B ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 : 
        key == KEY_X ? 0x20 : 
        1;   
        key =        
        key == KEY_2 ? KEY_ACORCHE : key == KEY_R ? KEY_COMA : key == KEY_T ? KEY_PUNTO : key == KEY_S ? KEY_MENOS :
        key == KEY_Y ? KEY_PTOCOMA : key == KEY_U ? KEY_COMILLA : key == KEY_O ? KEY_COMILLA : key == KEY_P ? KEY_2 :
        key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : key == KEY_Z ? KEY_PTOCOMA : key == KEY_H ? KEY_0 :
        key == KEY_J ? KEY_MENOS : key == KEY_K ? KEY_IGUAL : key == KEY_L ? KEY_BKSLASH : key == KEY_B ? KEY_CCORCHE :
        key == KEY_M ? KEY_PUNTO : key == KEY_N ? KEY_COMA : key == KEY_X ? KEY_APPS : key == KEY_D ? KEY_M :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_ht8
    case ht8:
        keymodif = 
        key == KEY_D ? 0 :
        key == KEY_H ? 0 :
        key == KEY_Y ? 0 :
        key == KEY_U ? 0 :
        key == KEY_2 ? 0 :        
        key == KEY_0 ? 0 :
        key == KEY_O ? 0 :
        key == KEY_J ? 0 :
        key == KEY_Z ? 0 :
        key == KEY_V ? 0 :
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :
        1; 
        key =
        key == KEY_D ? KEY_F2 : key == KEY_H ? KEY_F4 :
        key == KEY_2 ? KEY_BKSLASH : key == KEY_0 ? KEY_F1 : key == KEY_Y ? KEY_ACORCHE : key == KEY_U ? KEY_CCORCHE :
        key == KEY_O ? KEY_PTOCOMA : key == KEY_L ? KEY_IGUAL : key == KEY_Z ? KEY_MENOS : key == KEY_V ? KEY_SLASH :
        key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO : key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA :
        key == KEY_P ? KEY_2 : key == KEY_J ? KEY_IGUAL : key == KEY_K ? KEY_PTOCOMA : key == KEY_C ? KEY_SLASH :
        key == KEY_B ? KEY_MENOS :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_galak
        case galak:
        keymodif = 
        key == KEY_O ? 0 :
        key == KEY_L ? 0 :
        key == KEY_Z ? 0 :
        key == KEY_V ? 0 :
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :
        1; 
        key =
        key == KEY_O ? KEY_PTOCOMA : key == KEY_L ? KEY_IGUAL : key == KEY_Z ? KEY_ACORCHE : key == KEY_V ? KEY_SLASH :
        key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO : key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA :
        key == KEY_P ? KEY_2 : key == KEY_J ? KEY_IGUAL : key == KEY_K ? KEY_PTOCOMA : key == KEY_C ? KEY_SLASH :
        key == KEY_B ? KEY_ACORCHE :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_8 || key == KEY_9 || key == KEY_0) ? key : 0;
        break;
#endif

#ifdef symbols_multi
    case multi:
        keymodif = 
        key == KEY_7 ? 0 :
        key == KEY_Y ? 0 :
        key == KEY_U ? 0 :
        key == KEY_O ? 0 :
        key == KEY_J ? 0 :
        key == KEY_L ? 0 :
        key == KEY_V ? 0 :
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :
        1; 
        key =
        key == KEY_6 ? KEY_7 : key == KEY_8 ? KEY_9 : key == KEY_9 ? KEY_0 :
        key == KEY_7 ? KEY_COMILLA : key == KEY_0 ? KEY_MENOS : key == KEY_R ? KEY_COMA :
        key == KEY_T ? KEY_PUNTO : key == KEY_Y ? KEY_ACORCHE : key == KEY_U ? KEY_CCORCHE :
        key == KEY_O ? KEY_PTOCOMA : key == KEY_P ? KEY_COMILLA : key == KEY_F ? KEY_ACORCHE :
        key == KEY_G ? KEY_CCORCHE : key == KEY_H ? KEY_6 : key == KEY_J ? KEY_MENOS : 
        key == KEY_K ? KEY_IGUAL : key == KEY_L ? KEY_IGUAL : key == KEY_Z ? KEY_PTOCOMA :
        key == KEY_C ? KEY_SLASH : key == KEY_V ? KEY_SLASH : key == KEY_B ? KEY_8 :
        key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO :
        (key == KEY_1 || key == KEY_2 || key == KEY_3 || key == KEY_4 || key == KEY_5) ? key : 0;
        break;
#endif

#ifdef symbols_zx80
        case zx80:
        keymodif = 
        key == KEY_M ? 0 :
        1;
        key = 
        key == KEY_G ? KEY_3 : key == KEY_4 ? KEY_U : key == KEY_Y ? KEY_2 : key == KEY_1 ? KEY_ENTER :
        key == KEY_S ? KEY_1 : key == KEY_U ? KEY_B : key == KEY_F ? KEY_4 :
        key == KEY_8 ? KEY_I : key == KEY_9 ? KEY_O : key == KEY_P ? KEY_Y :
        key == KEY_B ? KEY_P : key == KEY_O ? KEY_X : key == KEY_R ? KEY_N : key == KEY_T ? KEY_M :
        key == KEY_N ? KEY_PUNTO : key == KEY_M ? KEY_PUNTO : key == KEY_X ? KEY_SPACE :
        (key == KEY_P || key == KEY_J || key == KEY_K || key == KEY_L || key == KEY_Z ||
        key == KEY_C || key == KEY_V || key == KEY_B || key == KEY_H) ? key : 0;        
        break;
#endif

#ifdef symbols_atom
        case atom:
        keymodif =      
        key == KEY_2 ? 0 :
        key == KEY_O ? 0 :
        key == KEY_V ? 0 : 
        key == KEY_Z ? 0 :
        key == KEY_J ? 0 : 
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :
        key == KEY_Y ? 0 :
        key == KEY_U ? 0 :        
        key == KEY_D ? 0 :        
        1; 
        key =
        key == KEY_2 ? KEY_IGUAL : key == KEY_T ? KEY_PUNTO : key == KEY_R ? KEY_COMA : key == KEY_D ? KEY_BKSLASH :         
        key == KEY_Y ? KEY_ACORCHE : key == KEY_U ? KEY_CCORCHE : key == KEY_O ? KEY_PTOCOMA : key == KEY_P ? KEY_2 :
        key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : key == KEY_Z ? KEY_COMILLA : key == KEY_B ? KEY_COMILLA : 
        key == KEY_J ? KEY_MENOS : key == KEY_K ? KEY_PTOCOMA : key == KEY_L ? KEY_MENOS : key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO :
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_cpet
        case cpet:
        keymodif = 

        key == KEY_L ? 0 : 
        key == KEY_J ? 0 : 
        key == KEY_Y ? 0 : 
        key == KEY_D ? 0 :
        key == KEY_U ? 0 :
        key == KEY_V ? 0 : 
        key == KEY_N ? 0 : 
        key == KEY_M ? 0 : 
        key == KEY_O ? 0 : 
        key == KEY_7 ? 0 :         
        
        1;   
        key =        
        key == KEY_6 ? KEY_7 : key == KEY_8 ? KEY_9 : key == KEY_9 ? KEY_0 : key == KEY_7 ? KEY_COMILLA : key == KEY_B ? KEY_8 :
        key == KEY_Y ? KEY_ACORCHE : key == KEY_U ? KEY_CCORCHE : key == KEY_H ? KEY_6 : key == KEY_P ? KEY_COMILLA : 
        key == KEY_J ? KEY_MENOS : key == KEY_K ? KEY_IGUAL : key == KEY_L ? KEY_IGUAL : key == KEY_D ? KEY_BKSLASH :
        key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : key == KEY_N ? KEY_COMA : key == KEY_M ? KEY_PUNTO :
        key == KEY_R ? KEY_COMA : key == KEY_T ? KEY_PUNTO : key == KEY_O ? KEY_PTOCOMA : key == KEY_Z ? KEY_PTOCOMA :         
        (key == KEY_1 || key == KEY_2 || key == KEY_3 || key == KEY_4 || key == KEY_5) ? key : 0;
        break;
#endif

#ifdef symbols_eg2000
        case eg2000:
        keymodif = 
        key == KEY_V ? 0 :
        key == KEY_Z ? 0 :
        key == KEY_2 ? 0 :
        key == KEY_O ? 0 :
        key == KEY_J ? 0 :
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :
        1; 
        key = 
        key == KEY_Z ? KEY_MENOS : key == KEY_B ? KEY_MENOS : key == KEY_2 ? KEY_ACORCHE : key == KEY_P ? KEY_2 : 
        key == KEY_C ? KEY_SLASH : key == KEY_V ? KEY_SLASH : key == KEY_O ? KEY_PTOCOMA : key == KEY_K ? KEY_PTOCOMA :
        key == KEY_L ? KEY_COMILLA : key == KEY_J ? KEY_COMILLA : key == KEY_N ? KEY_COMA : key == KEY_R ? KEY_COMA : 
        key == KEY_M ? KEY_PUNTO : key == KEY_T ? KEY_PUNTO : 
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9) ? key : 0;
        break;
#endif

#ifdef symbols_lynx
        case lynx:
        keymodif = 
        key == KEY_J ? 0 :
        key == KEY_2 ? 0 :
        key == KEY_Y ? 0 :
        key == KEY_U ? 0 :
        key == KEY_O ? 0 :
        key == KEY_Z ? 0 :
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :
        key == KEY_V ? 0 :
        key == KEY_Q ? 0x81 :
        1; 
        key = 
        key == KEY_J ? KEY_MENOS : key == KEY_L ? KEY_MENOS : key == KEY_2 ? KEY_IGUAL : key == KEY_X ? KEY_IGUAL : key == KEY_Q ? KEY_CCORCHE :
        key == KEY_Y ? KEY_ACORCHE : key == KEY_U ? KEY_CCORCHE : key == KEY_O ? KEY_PTOCOMA : key == KEY_K ? KEY_PTOCOMA : 
        key == KEY_Z ? KEY_COMILLA : key == KEY_B ? KEY_COMILLA : key == KEY_N ? KEY_COMA : key == KEY_R ? KEY_COMA : key == KEY_P ? KEY_2 : 
        key == KEY_M ? KEY_PUNTO : key == KEY_T ? KEY_PUNTO : key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : 
        (key == KEY_1 || key == KEY_3 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9 || key == KEY_0) ? key : 0;
        break;
#endif

#ifdef symbols_enterp
        case enterp:
        keymodif = 
        key == KEY_J ? 0 :
        key == KEY_2 ? 0 :
        key == KEY_Y ? 0 :
        key == KEY_U ? 0 :
        key == KEY_O ? 0 :
        key == KEY_Z ? 0 :
        key == KEY_N ? 0 :
        key == KEY_M ? 0 :
        key == KEY_V ? 0 : 
        key == KEY_D ? 0 : 
        key == KEY_H ? 0 : 
        key == KEY_3 ? 4 :
        key == KEY_Q ? 4 :        
        1; 
        key = 
        key == KEY_J ? KEY_MENOS : key == KEY_L ? KEY_MENOS : key == KEY_H ? KEY_IGUAL : key == KEY_A ? KEY_IGUAL : 
        key == KEY_2 ? KEY_ACORCHE : key == KEY_Y ? KEY_CCORCHE : key == KEY_F ? KEY_CCORCHE : key == KEY_P ? KEY_2 : key == KEY_X ? KEY_3 :
        key == KEY_U ? KEY_BKSLASH : key == KEY_G ? KEY_BKSLASH : key == KEY_Z ? KEY_COMILLA : key == KEY_B ? KEY_COMILLA :         
        key == KEY_O ? KEY_PTOCOMA : key == KEY_K ? KEY_PTOCOMA : key == KEY_N ? KEY_COMA : key == KEY_R ? KEY_COMA : 
        key == KEY_M ? KEY_PUNTO : key == KEY_T ? KEY_PUNTO : key == KEY_V ? KEY_SLASH : key == KEY_C ? KEY_SLASH : 
        key == KEY_D ? KEY_LESS : key == KEY_S ? KEY_LESS : key == KEY_3 ? KEY_A : key == KEY_Q ? KEY_ACORCHE :
        (key == KEY_1 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_7 || key == KEY_8 || key == KEY_9 || key == KEY_0) ? key : 0;
        break;
#endif

    default:
        key = 0;
        break;
        }
    }

    return;
}

void pulsateclaconsymbol(unsigned char row, unsigned char col, KBMODE modokb)
{
    // 0x1 -> LShift
    // 0x2 -> LCTrl
    // 0x4 -> LAlt
    // 0x8 -> RAlt (AltGr)
    // 0x10 -> RCTrl
    // 0x20 -> Requiere 0xE0
    // 0x40 -> AltCode
    // 0x80 -> Requiere SHIFT + ENTER (ZX81)
    // 0x80 -> Symbol (SAM)
    // 0x80 -> Copyright (LYNX)
    
    
    uint8_t key = 0, keymodif = 0;
    unsigned char shift = 0, ctrl = 0, rctrl = 0, alt = 0, altgr = 0, specialkey = 0, altcode = 0, zx81function = 0, samsymbol = 0, copyrightlynx = 0;
    typematicfirst = 0;
    typematic_codeaux = 0;
    key = codeset == 1 ? mapSET1[row][col] : mapZX[row][col];
  
    if (modo)
    {
        traducekey(key, keymodif, modokb);
        if (key)
        {
            if (modokb == pc && codeset == 1) { key = mapXT1[row][col]; shift = modXT1[row][col] & 1; altgr = modXT1[row][col] & 8; altcode = modXT1[row][col] & 0x40; }
            else 
            { 
                shift = (keymodif & 0x1) == 0x1;
                ctrl = (keymodif & 0x2) == 0x2;
                alt = (keymodif & 0x4) == 0x4;
                altgr = (keymodif & 0x8) == 0x8;
                rctrl = (keymodif & 0x10) == 0x10;
                specialkey = (keymodif & 0x20) == 0x20;
                altcode = (keymodif & 0x40) == 0x40;
                zx81function = (modo == zx81 && (keymodif & 0x80) == 0x80);
                samsymbol = (modo == sam && (keymodif & 0x80) == 0x80);
                copyrightlynx = (modo == lynx && (keymodif & 0x80) == 0x80);
            }
            
            if (key && !altcode)
            {
                if (copyrightlynx) {
                    sendPS2(KEY_LSHIFT);                    
                    sendPS2(KEY_ACORCHE);                    
                    _delay_ms(20);
                    sendPS2(0xF0);
                    sendPS2(KEY_ACORCHE);
                    _delay_ms(20);
                    sendPS2(KEY_LSHIFT);
                    _delay_ms(20);
                }
                if (shift) { if (codeset == 2) { sendPS2(KEY_LSHIFT); typematic_codeaux = KEY_LSHIFT; } else { sendPS2(KS1_LSHIFT); typematic_codeaux = KS1_LSHIFT; } }
                if (ctrl) { if (codeset == 2) { sendPS2(KEY_LCTRL); typematic_codeaux = KEY_LCTRL; } } 
                if (alt) { if (codeset == 2) { sendPS2(KEY_LALT); typematic_codeaux = KEY_LALT; } } 
                if (altgr) { sendPS2_aux(0xE0, 0); if (codeset == 2) sendPS2(KEY_RALT); else sendPS2(KS1_RALT); } 
                if (rctrl) { if (codeset == 2) { sendPS2_aux(0xE0, 0); sendPS2(KEY_RCTRL); } } 
                if (specialkey) sendPS2_aux(0xE0, 0);
                if (zx81function) {
                    sendPS2(KEY_LSHIFT);
                    sendPS2(KEY_ENTER);
                    _delay_ms(100);
                    sendPS2(0xF0);
                    sendPS2(KEY_ENTER);
                    sendPS2(0xF0);
                    sendPS2(KEY_LSHIFT);
                    _delay_ms(100);
                }
                if (samsymbol) {
                    sendPS2(KEY_TLD);
                    _delay_ms(100);
                }
                sendPS2((uint8_t)key);
                typematic_code = (uint8_t)key;

            }
        }
    }
    else
    {
        key = mapZX[row][col];
        sendPS2(KEY_LCTRL); typematic_codeaux = KEY_LCTRL;
        sendPS2((uint8_t)key);
        typematic_code = (uint8_t)key;
    }
}
void sueltateclaconsymbol(unsigned char row, unsigned char col, KBMODE modokb)
{
    // 0x1 -> LShift
    // 0x2 -> LCTrl
    // 0x4 -> LAlt
    // 0x8 -> RAlt (AltGr)
    // 0x10 -> RCTrl
    // 0x20 -> Requiere 0xE0
    // 0x40 -> AltCode
    // 0x80 -> Requiere SHIFT + ENTER (ZX81)
    // 0x80 -> Symbol (SAM)
    // 0x80 -> Copyright (LYNX)
  
    uint8_t key = 0, keymodif = 0;
    unsigned char shift = 0, ctrl = 0, rctrl = 0, alt = 0, altgr = 0, specialkey = 0, altcode = 0, zx81function = 0, samsymbol = 0, copyrightlynx = 0;
    unsigned char d1 = 0, d2 = 0, d3 = 0;
    typematic_code = 0;
    key = mapZX[row][col];
    if (modo)
    {
        traducekey(key, keymodif, modokb);
        if (key)
        {
            if (modokb == pc && codeset == 1) { key = mapXT1[row][col]; shift = modXT1[row][col] & 1; altgr = modXT1[row][col] & 8; altcode = modXT1[row][col] & 0x40; }
            else 
            { 
                shift = (keymodif & 0x1) == 0x1;
                ctrl = (keymodif & 0x2) == 0x2;
                alt = (keymodif & 0x4) == 0x4;
                altgr = (keymodif & 0x8) == 0x8;
                rctrl = (keymodif & 0x10) == 0x10;
                specialkey = (keymodif & 0x20) == 0x20;
                altcode = (keymodif & 0x40) == 0x40;
                zx81function = (modo == zx81 && (keymodif & 0x80) == 0x80);
                samsymbol = (modo == sam && (keymodif & 0x80) == 0x80);
                copyrightlynx = (modo == lynx && (keymodif & 0x80) == 0x80);
            }

            if (key)
            {
                if (!altcode)
                {
              
                    if (codeset == 2)
                    {
                        if (zx81function) _delay_ms(100);
                        if (specialkey) sendPS2_aux(0xE0, 0);
                        sendPS2(0xF0);
                        sendPS2((uint8_t)key);
                        if (samsymbol) {
                            sendPS2(0xF0);
                            sendPS2(KEY_TLD);                            
                        }

                        if (copyrightlynx) {                            
                            sendPS2(0xF0);                            
                            sendPS2(KEY_LSHIFT);                            
                        }
                    }
                    else sendPS2((uint8_t)key + KS1_RELEASE);

                    if (shift) { if (codeset == 2) { sendPS2(0xF0); sendPS2(KEY_LSHIFT); } else sendPS2(KS1_LSHIFT + KS1_RELEASE); }
                    if (ctrl) { if (codeset == 2) { sendPS2(0xF0); sendPS2(KEY_LCTRL); } }
                    if (alt) { if (codeset == 2) { sendPS2(0xF0); sendPS2(KEY_LALT); } }
                    if (altgr) { sendPS2(0xE0); if (codeset == 2) { sendPS2(0xF0); sendPS2(KEY_RALT); } else sendPS2(KS1_RALT + KS1_RELEASE); }
                    if (rctrl) { if (codeset == 2) { sendPS2(0xE0); sendPS2(0xF0); sendPS2(KEY_RCTRL); } }
                }
                else
                {
                    if (key == 126) { d1 = codeset == 2 ? KEYPAD_1 : KS1PAD_1; d2 = codeset == 2 ? KEYPAD_2 : KS1PAD_2; d3 = codeset == 2 ? KEYPAD_6 : KS1PAD_6; }
                    if (key == 156) { d1 = codeset == 2 ? KEYPAD_1 : KS1PAD_1; d2 = codeset == 2 ? KEYPAD_5 : KS1PAD_5; d3 = codeset == 2 ? KEYPAD_6 : KS1PAD_6; }
                  
                    if (codeset == 1) pulsa_alt_code_set1(d1, d2, d3);
                    if (codeset == 2) pulsa_alt_code_set2(d1, d2, d3);
                  
                }

            }

        }
    }
    else
    {
        key = mapZX[row][col];
        sendPS2(0xF0); sendPS2((uint8_t)key);
        sendPS2(0xF0); sendPS2(KEY_LCTRL);
    }
}
void pulsateclaconshift(unsigned char row, unsigned char col, unsigned char key)
{
    typematicfirst = 0;
    typematic_codeaux = 0;
    key = key == KEY_F2 && (modo == zx80 || modo == zx81) ? 0 : key; // En el core de ZX80/81 no se intercepta la tecla F2 como EDIT
    key = key == KEY_F2 && modo == sam ? KEY_RWIN : key; // En el core de Sam Coupe EDIT es la tecla derecha de Windows
    key = (modo == sam || modo == enterp)&& row == 6 && col == 0 ? KEY_TAB : key;
    if (!key) //si no esta mapeada saca la mayuscula
    {
        if (codeset == 2) { sendPS2(KEY_LSHIFT); typematic_codeaux = KEY_LSHIFT; } else { sendPS2(KS1_LSHIFT); typematic_codeaux = KS1_LSHIFT; }        
        if (codeset == 2) { sendPS2(mapZX[row][col]); typematic_code = mapZX[row][col]; } else { sendPS2(mapSET1[row][col]); typematic_code = mapSET1[row][col]; }
    }
    else
    {
        if (codeset == 2 && (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN || key == KEY_RWIN)) { sendPS2(0xE0); typematic_codeaux = 0xE0; } //Es una tecla del codeset2 que necesita E0
        if (codeset == 1 && (key == KS1_LEFT || key == KS1_RIGHT || key == KS1_UP || key == KS1_DOWN || key == KS1_RWIN)) { sendPS2(0xE0); typematic_codeaux = 0xE0; }//Es una tecla del codeset1 que necesita E0
        sendPS2(key);
        typematic_code = key;
    }
}

void sueltateclaconshift(unsigned char row, unsigned char col, unsigned char key)
{
    typematic_code = 0;
    key = key == KEY_F2 && (modo == zx80 || modo == zx81) ? 0 : key; // En el core de ZX80/81 no se intercepta la tecla F2 como EDIT
    key = key == KEY_F2 && modo == sam ? KEY_RWIN : key; // En el core de Sam Coupe EDIT es la tecla derecha de Windows
    key = (modo == sam || modo == enterp) && row == 6 && col == 0 ? KEY_TAB : key;
    if (!key) //si no esta mapeada saca la mayuscula
    {
        if (codeset == 2) { sendPS2(0xF0); sendPS2(mapZX[row][col]); sendPS2(0xF0); sendPS2(KEY_LSHIFT); }
        else { sendPS2(mapSET1[row][col] + KS1_RELEASE); sendPS2(KS1_LSHIFT + KS1_RELEASE); }
    }
    else
    {
        if (codeset == 2 && (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN || key == KEY_RWIN)) sendPS2(0xE0); //Es una tecla del codeset2 que necesita E0
        if (codeset == 1 && (key == KS1_LEFT || key == KS1_RIGHT || key == KS1_UP || key == KS1_DOWN || key == KS1_RWIN)) sendPS2(0xE0); //Es una tecla del codeset1 que necesita E0
        if (codeset == 2) { sendPS2(0xF0); sendPS2(key); }
        else sendPS2(key + KS1_RELEASE);
    }
}
#ifdef atmega644
void traduceextra2a(uint8_t r, uint8_t c, int8_t p)
{
    uint8_t isextra2a = 1;	
    uint8_t re = 0, ce = 0, rt = 0, ct = 0;
    uint8_t csss_status = 0;

    switch (mapZX[r][c])
    {

    case KEY_COMILLA:
        re = SYMBOL_SHIFT_ROW;
        ce = SYMBOL_SHIFT_COL;
        rt = Y_P_ROW;
        ct = P_COL;
        ss_counter += p;
        break;
    case KEY_PTOCOMA:
        re = SYMBOL_SHIFT_ROW;
        ce = SYMBOL_SHIFT_COL;
        rt = Y_P_ROW;
        ct = O_COL;
        ss_counter += p;
        break;
    case KEY_PUNTO:
        re = SYMBOL_SHIFT_ROW;
        ce = SYMBOL_SHIFT_COL;
        rt = B_M_ROW;
        ct = M_COL;
        ss_counter += p;
        break;
    case KEY_COMA:
        re = SYMBOL_SHIFT_ROW;
        ce = SYMBOL_SHIFT_COL;
        rt = B_M_ROW;
        ct = N_COL;
        ss_counter += p;
        break;
    case KEY_F1:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N1_N5_ROW;
        ct = N1_COL;
        cs_counter += p;
        break;
    case KEY_F3:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N1_N5_ROW;
        ct = N3_COL;
        cs_counter += p;
        break;
    case KEY_F4:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N1_N5_ROW;
        ct = N4_COL;
        cs_counter += p;
        break;
    case KEY_F9:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N6_N0_ROW;
        ct = N9_COL;
        cs_counter += p;
        break;
    case KEY_CAPS:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N1_N5_ROW;
        ct = N2_COL;
        cs_counter += p;
        break;
    case KEY_LEFT:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N1_N5_ROW;
        ct = N5_COL;
        cs_counter += p;
        break;
    case KEY_DOWN:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N6_N0_ROW;
        ct = N6_COL;
        cs_counter += p;
        break;
    case KEY_UP:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N6_N0_ROW;
        ct = N7_COL;
        cs_counter += p;
        break;
    case KEY_RIGHT:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N6_N0_ROW;
        ct = N8_COL;
        cs_counter += p;
        break;
#ifdef xchg_del_break
    case KEY_ESCAPE:
#else
    case KEY_BACKSP:
#endif
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = N6_N0_ROW;
        ct = N0_COL;
        cs_counter += p;
        break;
#ifdef xchg_del_break
    case KEY_BACKSP:
#else
    case KEY_ESCAPE:
#endif
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = SPACE_ROW;
        ct = SPACE_COL;
        cs_counter += p;
        break;
    case KEY_TAB:
        re = CAPS_SHIFT_ROW;
        ce = CAPS_SHIFT_COL;
        rt = SYMBOL_SHIFT_ROW;
        ct = SYMBOL_SHIFT_COL;
        cs_counter += p;
        break;

    default:
        isextra2a = 0;
        break;
    }

    if (isextra2a)
    {
        csss_status = matriz[rt][ct] & 0x18;
        if (p == -1)
        {    
            if (cs_counter == 0 && ss_counter == 0) matriz[re][ce] = matriz[r][c];
        }
        else matriz[re][ce] = matriz[r][c];
        matriz[rt][ct] = matriz[r][c] | csss_status;

    }
    
}
#endif

void pulsa_y_suelta_teclas_modificadoras(uint8_t control, uint8_t alt, uint8_t shift, uint8_t altgr, uint8_t lwin)
{

    if (control)
    {
        switch (kbcontrol)
        {
        case 1:
            if (codeset == 2) sendPS2_aux(KEY_LCTRL, 0); else sendPS2_aux(KS1_LCTRL, 0);
            kbcontrol = 2;
            break;
        case 2:
            _delay_ms(delay_specialkeys); // Antes de liberarla añadimos un pequeño delay
            if (codeset == 2) { sendPS2_aux(0xF0, 0); sendPS2_aux(KEY_LCTRL, 0); }
            else sendPS2_aux(KS1_LCTRL + KS1_RELEASE, 0);
            kbcontrol = 0;
            break;
        default:
            break;
        }
    }

    if (alt)
    {
        switch (kbalt)
        {
        case 1:
            if (codeset == 2) sendPS2_aux(KEY_LALT, 0); else sendPS2_aux(KS1_LALT, 0);
            kbalt = 2;
            break;
        case 2:
            _delay_ms(delay_specialkeys); // Antes de liberarla añadimos un pequeño delay
            if (codeset == 2) { sendPS2_aux(0xF0, 0); sendPS2_aux(KEY_LALT, 0); }
            else sendPS2_aux(KS1_LALT + KS1_RELEASE, 0);
            kbalt = 0;
            break;
        default:
            break;
        }
    }    

    if (shift)
    {
        switch (kbshift)
        {
        case 1:
            if (codeset == 2) sendPS2_aux(KEY_LSHIFT, 0); else sendPS2_aux(KS1_LSHIFT, 0);
            kbshift = 2;
            break;
        case 2:
            _delay_ms(delay_specialkeys); // Antes de liberarla añadimos un pequeño delay
            if (codeset == 2) { sendPS2_aux(0xF0, 0); sendPS2_aux(KEY_LSHIFT, 0); }
            else sendPS2_aux(KS1_LSHIFT + KS1_RELEASE, 0);
            kbshift = 0;
            break;
        default:
            break;
        }
    }

    if (altgr)
    {
        switch (kbaltgr)
        {
        case 1:
            if (codeset == 2) { sendPS2_aux(0xE0, 0); sendPS2_aux(KEY_RALT, 0); }
            else { sendPS2_aux(0xE0, 0); sendPS2_aux(KS1_RALT, 0); }
            kbaltgr = 2;
            break;
        case 2:
            _delay_ms(delay_specialkeys); // Antes de liberarla añadimos un pequeño delay
            if (codeset == 2) { sendPS2_aux(0xE0, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(KEY_RALT, 0); }
            else { sendPS2_aux(0xE0, 0); sendPS2_aux(KS1_RALT + KS1_RELEASE, 0); }
            kbaltgr = 0;
            break;
        default:
            break;
        }
    }

    if (lwin)
    {
        switch (kblwin)
        {
        case 1:
            if (codeset == 2) { sendPS2_aux(0xE0, 0); sendPS2_aux(KEY_LWIN, 0); }
            else { sendPS2_aux(0xE0, 0); sendPS2_aux(KS1_LWIN, 0); }
            kblwin = 2;
            break;
        case 2:
            _delay_ms(delay_specialkeys); // Antes de liberarla añadimos un pequeño delay
            if (codeset == 2) { sendPS2_aux(0xE0, 0); sendPS2_aux(0xF0, 0); sendPS2_aux(KEY_LWIN, 0); }
            else { sendPS2_aux(0xE0, 0); sendPS2_aux(KS1_LWIN + KS1_RELEASE, 0); }
            kblwin = 0;
            break;
        default:
            break;
        }
    }
    
}

void espera_a_soltar_tecla(uint8_t r, uint8_t c)
{
    //activar row/fila
    pinSet(pinsR[r], bcdR[r], _OUT);
    pinPut(pinsR[r], bcdR[r], LO);
    _delay_us(5);
    while (pinStat(pinsC[c], bcdC[c])) true;
    //desact. row/fila
    pinSet(pinsR[r], bcdR[r], _IN);
}

void matrixScan()
{
    uint8_t r, c;
    uint8_t keyaux = 0;

    fnpulsada = 0; //Se pone a 0 la pulsacion de una tecla de funcion

#ifdef switches
    uint8_t b;
    //Controla los Botones Externos del Arduino
    for (b = 0; b<NUMSW; b++)
    {
        if (pinStat(pinsSW[b], bcdSW[b]))
        {
            _delay_us(10); //debounce
            if (pinStat(pinsSW[b], bcdSW[b]))
            {
                if (b == 0) //NMI
                {
                    if (modo == zx || modo == cpc || modo == jup || modo == sam)
                    {
                        // Control+Alt+F5
                        pulsafn(Y_P_ROW, Y_COL, codeset == 1 ? KS1_F5 : KEY_F5, 0, 0, 1, 1, 0, 0);
                    }
                }    
                if (b == 1) //VGA-SWICH
                {
                    if (modo == zx || modo == at8 || modo == bbc || modo == vic || modo == ori || modo == ht8)
                    {
                        pulsafn(A_G_ROW, G_COL, codeset == 1 ? KS1_SCRLCK : KEY_SCRLCK, 0, 0, 1, 1, 0, 0);
                    }
                    if (modo == msx)
                    {
                        pulsaimprpant(A_G_ROW, G_COL);
                    }
                    fnpulsada = 1;
                }
                
            }
        }
    } //Fin de la comprobacion de botones
#endif
    rescan:

    //Escaneo de la matriz del teclado
    if (!fnpulsada) for (r = 0; r<ROWS; r++)
    {
        //activar row/fila
        pinSet(pinsR[r], bcdR[r], _OUT);
        pinPut(pinsR[r], bcdR[r], LO);
        _delay_us(5);
        for (c = 0; c<COLS; c++)
        {

            if (pinStat(pinsC[c], bcdC[c]))
            {
                _delay_us(10); //debounce
                if (pinStat(pinsC[c], bcdC[c]))
                {
                    if (matriz[r][c] & 0x01)
                    {
                        matriz[r][c] |= 0x02; //Marcado como mantenido "0x02"
                        #ifdef atmega644
                        if (r > 7 && c > 4) // pulsa extra +2A/+3
                        {
                            traduceextra2a(r, c, 0);
                        }
                        #endif
                    }
                    else
                    {
                        matriz[r][c] |= 0x01; // Marcado como pulsado "0x01"
                        #ifdef atmega644
                        if (r > 7 && c > 4) // pulsa extra +2A/+3
                        {
                            traduceextra2a(r, c, 1);
                        }
                        #endif
                    }

                }

                else if (matriz[r][c] & 0x01)
                { 
                    matriz[r][c] &= ~0x02; // Ya no esta mantenida "~0x02"
                    matriz[r][c] &= ~0x01; // Ya no esta pulsada "~0x01"
                    matriz[r][c] |= 0x04; // Marcado para soltar la tecla "0x04" (si entra por debounce)
                    #ifdef atmega644
                    if (r > 7 && c > 4) // suelta extra +2A/+3
                    {
                        traduceextra2a(r, c, -1);
                        matriz[r][c] = 0;
                    }
                    #endif

                } 
            }
            else if (matriz[r][c] & 0x01)
            { 
                matriz[r][c] &= ~0x02; // Ya no esta mantenida "~0x02"
                matriz[r][c] &= ~0x01; // Ya no esta pulsada "~0x01"
                matriz[r][c] |= 0x04; // Marcado para soltar la tecla "0x04"
                #ifdef atmega644
                if (r > 7 && c > 4) // suelta extra +2A/+3
                {
                    traduceextra2a(r, c, -1);
                    matriz[r][c] = 0;
                }
                #endif
            }

        }//Fin de Escaneo de las Columnas para el Row/Fila indicado
        //desact. row/fila
        pinSet(pinsR[r], bcdR[r], _IN);
    } //fin escaneo de Rows/Filas de la matriz del teclado	

    if (soltarteclas)
    {
        soltarteclas = 0;
        for (r = 0; r<ROWS8; r++) for (c = 0; c<COLS5; c++) if (matriz[r][c] & 0x01) soltarteclas = 1;
        goto rescan;
    }

    if (funcion || cambiomodo)
    {

        if (matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x04) { matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] = 0; if (!modo) { sendPS2_aux(0xF0, 0); sendPS2_aux(CAPS_SHIFT, 0); } }
        if (matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x04) { matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] = 0; if (!modo) { sendPS2_aux(0xF0, 0); sendPS2_aux(SYMBOL_SHIFT, 0); } }
        
        for (r = 0; r < ROWS8; r++) for (c = 0; c < COLS5; c++)
        {
            if (funcion)
            {
                if (matriz[r][c] & 0x01)
                {
                    ejecutarfuncion(mapZX[r][c]);
                    break;
                }
            }
            if (cambiomodo)
            {
                if (matriz[r][c] & 0x01) modo = cambiarmodo(((KBMODE)(mapMODO[r][c])));
            }
        }

        fnpulsada = 1; //Si no se pulsa ninguna tecla sigue en bucle hasta que se pulse
    }

    //Comprobacion de Teclas especiales al tener pulsado Caps Shift y Symbol Shift
    if (!fnpulsada && (matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x01) && (matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x01))
    {
        if (!fnpulsando)
        {//row, col, key, key_E0, shift, ctrl, alt, museg
     
            if (matriz[A_G_ROW][F_COL] & 0x01)
            {
                funcion = 1; fnpulsada = 1; soltarteclas = 1;
                _delay_ms(250);         
            }

            if ((matriz[Z_V_ROW][Z_COL] & 0x01) && (modo || fkbmode))
            {
                kbcontrol = !kbcontrol ? 1 : kbcontrol;
                pulsa_y_suelta_teclas_modificadoras(1, 0, 0, 0, 0);
                espera_a_soltar_tecla(Z_V_ROW, Z_COL);
       
            }

            if ((matriz[Z_V_ROW][C_COL] & 0x01) && (modo || fkbmode))
            {
                kbalt = !kbalt ? 1 : kbalt;
                pulsa_y_suelta_teclas_modificadoras(0, 1, 0, 0, 0);
                espera_a_soltar_tecla(Z_V_ROW, C_COL);

            }
            if ((matriz[A_G_ROW][A_COL] & 0x01) && (modo || fkbmode))
            {
                kbshift = !kbshift ? 1 : kbshift;
                pulsa_y_suelta_teclas_modificadoras(0, 0, 1, 0, 0);
                espera_a_soltar_tecla(A_G_ROW, A_COL);

            }

            if ((matriz[Z_V_ROW][X_COL] & 0x01) && (modo || fkbmode))
            {
                kblwin = !kblwin ? 1 : kblwin;
                pulsa_y_suelta_teclas_modificadoras(0, 0, 0, 0, 1);
                espera_a_soltar_tecla(Z_V_ROW, X_COL);

            }

            if ((matriz[B_M_ROW][M_COL] & 0x01) && (modo || fkbmode))
            {
                kbaltgr = !kbaltgr ? 1 : kbaltgr;
                pulsa_y_suelta_teclas_modificadoras(0, 0, 0, 1, 0);
                espera_a_soltar_tecla(B_M_ROW, M_COL);
            }

            // F1 a F12
            if ((matriz[N1_N5_ROW][N1_COL] & 0x01) && (modo || fkbmode)) pulsafn(N1_N5_ROW, N1_COL, codeset == 1 ? KS1_F1 : KEY_F1, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F1
            if ((matriz[N1_N5_ROW][N2_COL] & 0x01) && (modo || fkbmode)) pulsafn(N1_N5_ROW, N2_COL, codeset == 1 ? KS1_F2 : KEY_F2, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F2
            if ((matriz[N1_N5_ROW][N3_COL] & 0x01) && (modo || fkbmode)) pulsafn(N1_N5_ROW, N3_COL, codeset == 1 ? KS1_F3 : KEY_F3, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F3
            if ((matriz[N1_N5_ROW][N4_COL] & 0x01) && (modo || fkbmode)) pulsafn(N1_N5_ROW, N4_COL, codeset == 1 ? KS1_F4 : KEY_F4, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F4
            if ((matriz[N1_N5_ROW][N5_COL] & 0x01) && (modo || fkbmode)) pulsafn(N1_N5_ROW, N5_COL, codeset == 1 ? KS1_F5 : KEY_F5, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F5
            if ((matriz[N6_N0_ROW][N6_COL] & 0x01) && (modo || fkbmode)) pulsafn(N6_N0_ROW, N6_COL, codeset == 1 ? KS1_F6 : KEY_F6, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F6 
            if ((matriz[N6_N0_ROW][N7_COL] & 0x01) && (modo || fkbmode)) pulsafn(N6_N0_ROW, N7_COL, codeset == 1 ? KS1_F7 : KEY_F7, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F7
            if ((matriz[N6_N0_ROW][N8_COL] & 0x01) && (modo || fkbmode)) pulsafn(N6_N0_ROW, N8_COL, codeset == 1 ? KS1_F8 : KEY_F8, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F8
            if ((matriz[N6_N0_ROW][N9_COL] & 0x01) && (modo || fkbmode)) pulsafn(N6_N0_ROW, N9_COL, codeset == 1 ? KS1_F9 : KEY_F9, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);     //F9
            if ((matriz[N6_N0_ROW][N0_COL] & 0x01) && (modo || fkbmode)) pulsafn(N6_N0_ROW, N0_COL, codeset == 1 ? KS1_F10 : KEY_F10, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);   //F10
            if ((matriz[Q_T_ROW][Q_COL] & 0x01) && (modo || fkbmode)) pulsafn(Q_T_ROW, Q_COL, codeset == 1 ? KS1_F11 : KEY_F11, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);         //F11
            if ((matriz[Q_T_ROW][W_COL] & 0x01) && (modo || fkbmode)) pulsafn(Q_T_ROW, W_COL, codeset == 1 ? KS1_F12 : KEY_F12, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);        	//F12
            //

            // Teclas especiales
            if ((matriz[A_G_ROW][S_COL] & 0x01) && (modo || fkbmode)) pulsafn(A_G_ROW, S_COL, codeset == 1 ? KS1_LESS : KEY_LESS, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);       // (Z) KEY_LESS

            if ((matriz[Y_P_ROW][I_COL] & 0x01) && (modo || fkbmode)) pulsafn(Y_P_ROW, I_COL, codeset == 1 ? KS1_CAPS : (modo == vic || modo == c16) ? KEY_BLOQNUM : modo == cpet ? KEY_APPS : KEY_CAPS, modo == cpet, kbshift, kbcontrol, kbalt, kbaltgr, kblwin); // (I) Bloq Mayús (Bloq Num para C16 y VIC20)
            if ((matriz[Y_P_ROW][O_COL] & 0x01) && (modo || fkbmode)) pulsafn(Y_P_ROW, O_COL, codeset == 1 ? KS1_ACORCHE : KEY_ACORCHE, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin); // (O) KEY_ACORCHE
            if ((matriz[Y_P_ROW][P_COL] & 0x01) && (modo || fkbmode)) pulsafn(Y_P_ROW, P_COL, codeset == 1 ? KS1_CCORCHE : KEY_CCORCHE, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin); // (P) KEY_CCORCHE

            if ((matriz[Q_T_ROW][E_COL] & 0x01) && (modo || fkbmode)) pulsafn(Q_T_ROW, E_COL, codeset == 1 ? KS1PAD_ASTERISK : KEYPAD_ASTERISK, 0, kbshift, kbcontrol, kbalt, kbaltgr,  kblwin);     // (E) Num *
            if ((matriz[Q_T_ROW][R_COL] & 0x01) && (modo || fkbmode)) pulsafn(Q_T_ROW, R_COL, codeset == 1 ? KS1PAD_MENOS : KEYPAD_MENOS, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (R) Num -
            if ((matriz[A_G_ROW][D_COL] & 0x01) && (modo || fkbmode)) pulsafn(A_G_ROW, D_COL, codeset == 1 ? KS1PAD_MAS : KEYPAD_MAS, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);   // (D) Num +

            if ((matriz[Q_T_ROW][T_COL] & 0x01) && (modo || fkbmode)) { pulsaimprpant(Q_T_ROW, T_COL); is_fn(Q_T_ROW, T_COL); }  // (T) Impr Pant
            if ((matriz[Y_P_ROW][Y_COL] & 0x01) && (modo || fkbmode)) pulsafn(Y_P_ROW, Y_COL, codeset == 1 ? KS1_SCRLCK : KEY_SCRLCK, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (Y) Bloq Despl
            if ((matriz[Y_P_ROW][U_COL] & 0x01) && (modo || fkbmode)) { pulsapausa(Y_P_ROW, U_COL); is_fn(Y_P_ROW, U_COL); }       // (U) Pausa
            if ((matriz[A_G_ROW][G_COL] & 0x01) && (modo || fkbmode)) pulsafn(A_G_ROW, G_COL, codeset == 1 ? KS1_INS : KEY_INS, 1, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (G) Insert
            if ((matriz[H_L_ROW][H_COL] & 0x01) && (modo || fkbmode)) pulsafn(H_L_ROW, H_COL, codeset == 1 ? KS1_HOME : KEY_HOME, 1, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (H) Home
            if ((matriz[H_L_ROW][J_COL] & 0x01) && (modo || fkbmode)) pulsafn(H_L_ROW, J_COL, codeset == 1 ? KS1_PGUP : KEY_PGUP, 1, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (J) Re Pag     
            if ((matriz[Z_V_ROW][V_COL] & 0x01) && (modo || fkbmode)) pulsafn(Z_V_ROW, V_COL, codeset == 1 ? KS1_DELETE : KEY_DELETE, 1, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (V) Supr
            if ((matriz[B_M_ROW][B_COL] & 0x01) && (modo || fkbmode)) pulsafn(B_M_ROW, B_COL, codeset == 1 ? KS1_END : KEY_END, 1, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (B) End
            if ((matriz[B_M_ROW][N_COL] & 0x01) && (modo || fkbmode)) pulsafn(B_M_ROW, N_COL, codeset == 1 ? KS1_PGDW : KEY_PGDW, 1, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (N) Av Pag
            if ((matriz[H_L_ROW][K_COL] & 0x01) && (modo || fkbmode)) pulsafn(H_L_ROW, K_COL, codeset == 1 ? KS1_PTOCOMA : KEY_PTOCOMA, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             //  (K) KEY_PTOCOMA
            if ((matriz[H_L_ROW][L_COL] & 0x01) && (modo || fkbmode)) pulsafn(H_L_ROW, L_COL, codeset == 1 ? KS1_COMILLA : KEY_COMILLA, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);             // (L) KEY_COMILLA
            if ((matriz[ENTER_ROW][ENTER_COL] & 0x01) && (modo || fkbmode)) pulsafn(ENTER_ROW, ENTER_COL, codeset == 1 ? KS1_BKSLASH : KEY_BKSLASH, 0, kbshift, kbcontrol, kbalt, kbaltgr, kblwin);    // (ENTER) KEY_BKSLASH

        }
    }
    else fnpulsando = 0; //Fin de escaneo de combos

    //Control de teclado
    if (!fnpulsada) //Si no se ha pulsado ningun tecla de funcion y el modo es 0 (ZX-Spectrum)
    {
        if (!modo && !fkbmode)
        {
            //Enviar la pulsacion de Caps Shift y/o Symbol Shift si estamos en modo ZX)
            if ((matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x01) && !(matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x02)) { sendPS2(CAPS_SHIFT);    matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] |= 0x02;     espera++; } // Probar a suprimir matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] |= 0x02; (Ya se deja mantenido durante el scan)
            if ((matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x01) && !(matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x02)) { sendPS2(SYMBOL_SHIFT);  matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] |= 0x02; espera++; } // Probar a suprimir matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] |= 0x02; (Ya se deja mantenido durante el scan)
            if (matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x04) { sendPS2(0xF0); sendPS2(CAPS_SHIFT);    matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] = 0;     espera++; }
            if (matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x04) { sendPS2(0xF0); sendPS2(SYMBOL_SHIFT);  matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] = 0; espera++; }
            if (espera) { _delay_us(5); espera = 0; }
            //Enviar el resto de Teclas Pulsadas, nunca entrara por shift o symbol de matrix[r][c] ya que se cambia el valor en el bloque anterior a 3 o 0
            for (r = 0; r<ROWS8; r++) for (c = 0; c<COLS5; c++)
            {
                if ((matriz[r][c] & 0x01) && !(matriz[r][c] & 0x02)) sendPS2(mapZX[r][c]);
                if (matriz[r][c] & 0x04) { sendPS2(0xF0); sendPS2(mapZX[r][c]); matriz[r][c] = 0; }
            }
        }
        else // Manejo de los otros modos de Keymap
        {

            if (!antighosting && cs_counter == 0 && ss_counter == 0)
            {
                if (((matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x01) && !(matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x02)) ||
                    ((matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x01) && !(matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x02)))
                {
                    for (r = 0; r < ROWS8; r++) for (c = 0; c < COLS5; c++)
                    {
                        // Ignoramos teclas capssymbol y symbolshift en el recorrido
                        if (r == CAPS_SHIFT_ROW && c == CAPS_SHIFT_COL) continue;
                        if (r == SYMBOL_SHIFT_ROW && c == SYMBOL_SHIFT_COL) continue;
                        if (matriz[r][c] & 0x01) { antighosting = 1; break; }
                    }

                }
            }
            else
            {
                if (!(matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x01) && !(matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x01))
                    antighosting = 0;
            }
            
            for (r = 0; r<ROWS8; r++) for (c = 0; c<COLS5; c++)
            {
                // Ignoramos teclas capssymbol y symbolshift en el recorrido
                if (r == CAPS_SHIFT_ROW && c == CAPS_SHIFT_COL) continue;
                if (r == SYMBOL_SHIFT_ROW && c == SYMBOL_SHIFT_COL) continue;

                if ((matriz[r][c] & 0x01) && !(matriz[r][c] & 0x02)) // Gestion de pulsado
                {
                    if (!antighosting)
                    {
                        if (matriz[CAPS_SHIFT_ROW][CAPS_SHIFT_COL] & 0x01) matriz[r][c] |= 0x08; // Se marca capsshift
                        if (matriz[SYMBOL_SHIFT_ROW][SYMBOL_SHIFT_COL] & 0x01) matriz[r][c] |= 0x10; // Se marca symbolshift						
                    }

                    if ((matriz[r][c] & 0x08) && !(matriz[r][c] & 0x10)) pulsateclaconshift(r, c, codeset == 2 ? mapEXT[r][c] : mapEXT1[r][c]); // Pulsar con capsshift
                    if ((matriz[r][c] & 0x10) && !(matriz[r][c] & 0x08)) pulsateclaconsymbol(r, c, modo); // Pulsar con symbolshift
                    if (!(matriz[r][c] & 0x08) && !(matriz[r][c] & 0x10))
                    {
                        
                        if (opqa_cursors || opqa_numbers)
                        {
                            typematicfirst = 0;
                            typematic_codeaux = 0;
                            
                            if (codeset == 2)
                            {
                                keyaux = mapZX[r][c];
                                keyaux = keyaux == KEY_O ? KEY_LEFT : keyaux;
                                keyaux = keyaux == KEY_P ? KEY_RIGHT : keyaux;
                                keyaux = keyaux == KEY_Q ? KEY_UP : keyaux;
                                keyaux = keyaux == KEY_A ? KEY_DOWN : keyaux;    
                                if (opqa_cursors_m) keyaux = keyaux == KEY_M ? (opqa_cursors || modo == jup) ? KEY_SPACE : KEYPAD_0 : keyaux == KEY_SPACE ? opqa_cursors ? KEY_M : keyaux : keyaux;

                                if ((keyaux == KEY_LEFT || keyaux == KEY_RIGHT || keyaux == KEY_UP || keyaux == KEY_DOWN) && opqa_cursors) typematic_codeaux = 0xE0; //Es una tecla del codeset2 que necesita E0

                            }
                            if (codeset == 1)
                            {
                                keyaux = mapSET1[r][c];
                                keyaux = keyaux == KS1_O ? KS1_LEFT : keyaux;
                                keyaux = keyaux == KS1_P ? KS1_RIGHT : keyaux;
                                keyaux = keyaux == KS1_Q ? KS1_UP : keyaux;
                                keyaux = keyaux == KS1_A ? KS1_DOWN : keyaux;
                                if (opqa_cursors_m) keyaux = keyaux == KS1_M ? KS1_SPACE : keyaux;
                                if (keyaux == KS1_LEFT || keyaux == KS1_RIGHT || keyaux == KS1_UP || keyaux == KS1_DOWN) typematic_codeaux = 0xE0; //Es una tecla del codeset1 que necesita E0
                            }
                            typematic_code = keyaux;
                            if (typematic_codeaux > 0) sendPS2(0xE0);
                            sendPS2(typematic_code);
                        }
                        else
                        {
                            typematicfirst = 0;
                            if (codeset == 2) typematic_code = mapZX[r][c];
                            else typematic_code = mapSET1[r][c]; // Pulsar sin modificadores
                            typematic_codeaux = 0;
                            sendPS2(typematic_code);
                        }
                    }
                    // Si estan pulsados capsshift y symbolshift, no hacemos nada

                    matriz[r][c] |= 0x02; // Se marca mantenida
                }

                if ((matriz[r][c] & 0x04)) // Gestion de liberado
                {
                    if ((matriz[r][c] & 0x08) && !(matriz[r][c] & 0x10)) sueltateclaconshift(r, c, codeset == 2 ? mapEXT[r][c] : mapEXT1[r][c]); // Liberar con capsshift
                    if ((matriz[r][c] & 0x10) && !(matriz[r][c] & 0x08)) sueltateclaconsymbol(r, c, modo); // Liberar con symbolshift					
                    if (!(matriz[r][c] & 0x08) && !(matriz[r][c] & 0x10))
                    {
                        if (opqa_cursors || opqa_numbers)
                        {
                            typematic_code = 0;
                            
                            if (codeset == 2)
                            {
                                keyaux = mapZX[r][c];
                                keyaux = keyaux == KEY_O ? KEY_LEFT : keyaux;
                                keyaux = keyaux == KEY_P ? KEY_RIGHT : keyaux;
                                keyaux = keyaux == KEY_Q ? KEY_UP : keyaux;
                                keyaux = keyaux == KEY_A ? KEY_DOWN : keyaux;
                                if (opqa_cursors_m) keyaux = keyaux == KEY_M ? (opqa_cursors || modo == jup) ? KEY_SPACE : KEYPAD_0 : keyaux == KEY_SPACE ? opqa_cursors ? KEY_M : keyaux : keyaux;

                                if ((keyaux == KEY_LEFT || keyaux == KEY_RIGHT || keyaux == KEY_UP || keyaux == KEY_DOWN) && opqa_cursors) sendPS2(0xE0); //Es una tecla del codeset2 que necesita E0
                                sendPS2(0xF0); sendPS2(keyaux);
                            }
                            if (codeset == 1)
                            {
                                keyaux = mapSET1[r][c];
                                keyaux = keyaux == KS1_O ? KS1_LEFT : keyaux;
                                keyaux = keyaux == KS1_P ? KS1_RIGHT : keyaux;
                                keyaux = keyaux == KS1_Q ? KS1_UP : keyaux;
                                keyaux = keyaux == KS1_A ? KS1_DOWN : keyaux;
                                if (opqa_cursors_m) keyaux = keyaux == KS1_M ? KS1_SPACE : keyaux;

                                if (keyaux == KS1_LEFT || keyaux == KS1_RIGHT || keyaux == KS1_UP || keyaux == KS1_DOWN) sendPS2(0xE0); //Es una tecla del codeset 1 que necesita E0
                                sendPS2(keyaux + KS1_RELEASE);
                            }
                        }
                        else
                        {
                            if (codeset == 2) { sendPS2(0xF0); sendPS2(mapZX[r][c]); typematic_code = typematic_code == mapZX[r][c] ? 0 : typematic_code; }
                            else { sendPS2(mapSET1[r][c] + KS1_RELEASE); typematic_code = typematic_code == mapSET1[r][c] ? 0 : typematic_code; } // Liberar sin modificadores
                        }
                    }
                    matriz[r][c] = 0; // Fin de gestion de la tecla

                }
            }

        }//Fin del If/else manejo de modo ZX u otros Keymaps
    }//Fin del If del control del teclado.
}//FIN de Matrixscan

void setup()
{
    CPU_PRESCALE(CPU_16MHz);
    ps2Init();
    matrixInit();
    const uint8_t ZXUNO_SIGNATURE[] = { 'Z','X','U','N','O' };
    uint8_t checksignature[5];
    uint8_t issigned = 1;
    eeprom_read_block((void*)&checksignature, (const void*)0, 5);
    
    for (int n = 0; n < 5; n++) if (checksignature[n] != ZXUNO_SIGNATURE[n]) issigned = 0;
    if (issigned)
    {
        modo = cambiarmodo2(((KBMODE)eeprom_read_byte((uint8_t*)5)));
        fkbmode = eeprom_read_byte((uint8_t*)6);
    fkbmode = fkbmode > 1 ? 0 : fkbmode;
    }
    else
    {
        eeprom_write_block((const void*)&ZXUNO_SIGNATURE, (void*)0, 5); // Guardamos la firma
        eeprom_write_byte((uint8_t*)5, (uint8_t)0); // Guardamos modo ZX por defecto
        eeprom_write_byte((uint8_t*)6, (uint8_t)1); // Guardamos ZXFULLCOMBOS por defecto
    }
}

void loop()
{
    if (ps2Stat() && modo == MAXKB && (kbescucha || timeout_escucha > 0)) // Lineas CLK y/o DATA a 0 y escucha activa
    {                                                                     // Solo hay escucha activa en modo PC, hasta su inicializacion.
                                                                          // Una vez completada la inicializacion de teclado, no es necesario mantener activa la escucha de comandos excepto si se hace eco

        while (checkState(1000)) // tramos de 5 us (5000 us)
        {
            hostdataAnt = hostdata;
            if (getPS2(&hostdata) == 0)
            {
                timeout_escucha = 100000;   // Dejamos tiempo para que se complete la inicializacion
                if (hostdata == 0xEE)
                {
                    sendPS2(0xEE); // Echo
                    kbescucha = 1; // Si se hace eco, mantenemos la escucha de comandos (necesario en determinados adaptadores PS/2 -> USB)
                }
                else
                {
                    sendPS2(0xFA); // Ack
                }
                switch (hostdata)
                {
                    case 0x00: // second bit of 0xED or 0xF3 (or get scancode set)  
                        if (hostdataAnt == 0xF0)
                        {
                            sendPS2(codeset);
                        }
                        break;
                    case 0x01: // set scancode 1
                        if (hostdataAnt == 0xF0)
                        {
                            codeset = 1;
                        }
                        break;
                    case 0x02: // set scancode 2
                        if (hostdataAnt == 0xF0)
                        {
                            codeset = 2;
                        }
                        break;
                    case 0xED: // set/reset LEDs
                        break;
                    case 0xF2: // ID          
                        sendPS2(0xAB);
                        sendPS2(0x83);
                        break;
                    case 0xF0: // get/set scancode set
                        break;
                    case 0xF3: // set/reset typematic delay
                        break;
                    case 0xF4: // keyboard is enabled, break loop
                        break;
                    case 0xF5: // keyboard is disabled, break loop            
                        break;
                    case 0xFF:
                        // tell host we are ready to connect
                        sendPS2(0xAA); 
                        kbescucha = 0; // En el inicio iniciamos la cuenta atras de timeout
                        break;
                    default:
                        break;
                } //Fin del Swich
            } //Fin del IF de si detecta dato
        } //Fin del while que chequea el estado

    }
    else
    {
        if (timeout_escucha > 0) timeout_escucha--;
        if (modo == MAXKB && typematic_code != 0 && (typematicfirst++ > 1000 || codeset == 2) && typematic++ > 150) // Funcion tipematica simulada para PC
        {
            if (typematic_codeaux != 0) sendPS2(typematic_codeaux);
            sendPS2(typematic_code); typematic = 0;
        }
        matrixScan(); //No llegan datos del Host, enviamos teclado.
    }
}
