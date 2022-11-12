/*
 * Copyright 2010-2012,2015,2017,2022 Adrià Giménez Pastor.
 *
 * This file is part of adriagipas/Z80.
 *
 * adriagipas/Z80 is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * adriagipas/Z80 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with adriagipas/Z80.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 *  Z80.h - Simulador del processador Z80 escrit en ANSI C.
 *
 *  NOTES: Hem negue a utilitzar el truc de struct { b l,h; };.
 *
 *  NOTES: Necessita que s'implementen les següents funcions:
 *
 *          - Z80u8 Z80_read ( Z80u16 addr );
 *          - void Z80_write ( Z80u16 addr, Z80u8 data );
 *          - void Z80_reti_signal ();
 *
 *  TIMING: Si he entès bé, el Z80 te cicles màquina M i cicles de
 *          rellotge T. Cada cicle màquina pot durar de 4 a 5(tal
 *          vegada 6 no estic segur) cicles T, segons li convinga a la
 *          unitat de control. Cada instrucció necessita una serie de
 *          cicles T repartits en un número concret de cicles M. En
 *          principi en M1 els 3 primers cicles T són comuns a totes
 *          les instruccions is es corresponen amb la fase 'fetch'. La
 *          unitat de control en la mida de lo possible intenta fer
 *          coincidir la fase 'fetch' de la següent instrucció amb
 *          alguna fase M de l'actual instrucció, estalviant cicles T
 *          des del punt de vista de l'usuari. Açò es pot traduir en
 *          què les fases M d'una instrucció es modifiquen i allarguen
 *          per què resulta mes convenient.
 *
 *  TIMING: En esta primera versió vaig a assumir que la unitat de
 *  control és tonta i mai es capaç de fer coincidir la fase fetch
 *
 */

#ifndef __Z80_H__
#define __Z80_H__

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


/*********/
/* TIPUS */
/*********/

/* Tipus booleà. */
typedef enum
  {
    Z80_FALSE= 0,
    Z80_TRUE
  } Z80_Bool;

/* Tipus sencers. */
typedef int8_t Z80s8;
typedef uint8_t Z80u8;
typedef int16_t Z80s16;
typedef uint16_t Z80u16;
typedef uint32_t Z80u32;

/* Funció per a emetre avisos. */
typedef void
(Z80_Warning) (
               void       *udata,
               const char *format,
               ...
               );

/* Mnemonics. */
typedef enum
  {
    Z80_UNK= 0,
    Z80_LD,
    Z80_PUSH,
    Z80_POP,
    Z80_EX,
    Z80_EXX,
    Z80_LDI,
    Z80_LDIR,
    Z80_LDD,
    Z80_LDDR,
    Z80_CPI,
    Z80_CPIR,
    Z80_CPD,
    Z80_CPDR,
    Z80_ADD,
    Z80_ADC,
    Z80_SUB,
    Z80_SBC,
    Z80_AND,
    Z80_OR,
    Z80_XOR,
    Z80_CP,
    Z80_INC,
    Z80_DEC,
    Z80_DAA,
    Z80_CPL,
    Z80_NEG,
    Z80_CCF,
    Z80_SCF,
    Z80_NOP,
    Z80_HALT,
    Z80_DI,
    Z80_EI,
    Z80_IM0,
    Z80_IM1,
    Z80_IM2,
    Z80_RLCA,
    Z80_RLA,
    Z80_RRCA,
    Z80_RRA,
    Z80_RLC,
    Z80_RL,
    Z80_RRC,
    Z80_RR,
    Z80_SLA,
    Z80_SRA,
    Z80_SRL,
    Z80_RLD,
    Z80_RRD,
    Z80_BIT,
    Z80_SET,
    Z80_RES,
    Z80_JP,
    Z80_JR,
    Z80_DJNZ,
    Z80_CALL,
    Z80_RET,
    Z80_RETI,
    Z80_RETN,
    Z80_RST00,
    Z80_RST08,
    Z80_RST10,
    Z80_RST18,
    Z80_RST20,
    Z80_RST28,
    Z80_RST30,
    Z80_RST38,
    Z80_IN,
    Z80_INI,
    Z80_INIR,
    Z80_IND,
    Z80_INDR,
    Z80_OUT,
    Z80_OUTI,
    Z80_OTIR,
    Z80_OUTD,
    Z80_OTDR
  } Z80_Mnemonic;

/* Tipus d'operador. */
typedef enum
  {
    
    Z80_NONE= 0,
    
    /* Registres de 8 bits. */
    Z80_A,
    Z80_B,
    Z80_C,
    Z80_D,
    Z80_E,
    Z80_H,
    Z80_L,
    Z80_I,
    Z80_R,
    
    Z80_BYTE,
    
    /* Adreces contingudes en registres de 16 bits (HL),(BC)... */
    Z80_pHL,
    Z80_pBC,
    Z80_pDE,
    Z80_pSP,
    Z80_pIX,
    Z80_pIY,
    
    /* Adreces contingudes en regitre I* més desplaçament. */
    Z80_pIXd,
    Z80_pIYd,
    
    Z80_ADDR,
    
    /* Registres de 16 bits. */
    Z80_BC,
    Z80_DE,
    Z80_HL,
    Z80_SP,
    Z80_IX,
    Z80_IXL,
    Z80_IXH,
    Z80_IY,
    Z80_IYL,
    Z80_IYH,
    Z80_AF,
    Z80_AF2,
    
    /* Bits. */
    Z80_B0,
    Z80_B1,
    Z80_B2,
    Z80_B3,
    Z80_B4,
    Z80_B5,
    Z80_B6,
    Z80_B7,
    
    Z80_WORD,
    
    /* Condicions. */
    Z80_F_NZ,
    Z80_F_Z,
    Z80_F_NC,
    Z80_F_C,
    Z80_F_PO,
    Z80_F_PE,
    Z80_F_P,
    Z80_F_M,
    
    /* Displacement. */
    Z80_BRANCH,
    
    /* Registres de 8 bits entre paréntesis. */
    Z80_pB,
    Z80_pC,
    Z80_pD,
    Z80_pE,
    Z80_pH,
    Z80_pL,
    Z80_pA,
    
    Z80_pBYTE
    
  } Z80_OpType;

/* Identifica a una instrucció. */
typedef struct
{
  
  Z80_Mnemonic name;        /* Nom. */
  Z80_OpType   op1;         /* Primer operant. */
  Z80_OpType   op2;         /* Segon operant. */
  
} Z80_InstId;

/* Dades relacionades amb els operadors. */
typedef union
{
  
  Z80u8  byte;           /* Byte. */
  Z80s8  desp;           /* Desplaçament. */
  Z80u16 addr_word;      /* Adreça o paraula. */
  struct
  {
    Z80s8  desp;
    Z80u16 addr;
  }      branch;         /* Bot. */
  
} Z80_InstExtra;

/* Estructura per a desar tota la informació relativa a una
 * instrucció.
 */
typedef struct
{
  
  Z80_InstId    id;          /* Identificador d'instrucció. */
  Z80_InstExtra e1;          /* Dades extra operador1. */
  Z80_InstExtra e2;          /* Dades extra operador2. */
  Z80u8         bytes[4];    /* Bytes */
  Z80u8         nbytes;      /* Número de bytes. */
  
} Z80_Inst;

/* Tipus de pas d'execució. */
typedef struct
{
  
  enum {
    Z80_STEP_INST,
    Z80_STEP_NMI,
    Z80_STEP_IRQ
  } type;    /* Tipus. */
  union
  {
    Z80_Inst inst;    /* Instrucció decodificada. */
    Z80u8    bus;     /* Valor en el 'bus' per a les interrupcions
        		 IRQ. */
  } val;    /* Valor. */
} Z80_Step;



/************/
/* FUNCIONS */
/************/

/* Descodifica en INST la instrucció de l'adreça indicada i torna
 * l'adreça de la següent instrucció en memòria.
 */
Z80u16
Z80_decode (
            Z80u16    addr,
            Z80_Inst *inst
            );

/* Descodifica en STEP el següent pas i torna l'adreça de la
 * següent instrucció en memòria.
 */
Z80u16
Z80_decode_next_step (
        	      Z80_Step *step
        	      );

/* Inicia el mòdul. */
void
Z80_init (
          Z80_Warning *warning,    /* Funció per a mostrar avisos. */
          void        *udata       /* Dades de l'usuari. */
          );

/* Com Z80_init però no cal passar callbacks. S'ha de cridar després
 * de Z80_init.
 */
void
Z80_init_state (void);

/* Llig un byte del port indicat. */
Z80u8
Z80_io_read (
             Z80u8 port    /* Port. */
             );

/* Escriu un byte en el port indicat. */
void
Z80_io_write (
              Z80u8 port,    /* Port. */
              Z80u8 data     /* Dades. */
              );

/* Activa/Desactiva la línia d'interrupció IRQ, ficant DATA en el
 * bus. En el mode 1 el valor de DATA no importa.
 */
void
Z80_IRQ (
         const Z80_Bool active,
         const Z80u8    data
         );

/* Carrega l'estat del processador des d'un fitxer. Torna 0 si tot ha
 * anat bé.
 */
int
Z80_load_state (
        	FILE *f
        	);

/* Força una interrupció NMI, que s'executarà la pròxima vegada que
 * s'execute el processador. La interrupció NMI és un impuls.
 */
void
Z80_NMI (void);

/* Llig un byte de l'adreça indicada. */
Z80u8
Z80_read (
          Z80u16 addr    /* Adreça. */
          );

/* Reseteja el processador. */
void
Z80_reset (void);

/* Aquesta funció és cridada per el simulador per a indiciar que una
   interrupció ha acabat. */
void
Z80_reti_signal (void);

/* Desa l'estat del processador en un fitxer. Torna 0 si tot ha anat
 * bé.
 */
int
Z80_save_state (
        	FILE *f
        	);

/* Escriu un byte en l'adreça indicada. */
void
Z80_write (
           Z80u16 addr,    /* Adreça. */
           Z80u8  data     /* Dada. */
           );

/* Executa la següent instrucció, torna els cicles consumits. */
int
Z80_run (void);


#endif /* __Z80_H__ */
