/*
 * Copyright 2010-2013,2022 Adrià Giménez Pastor.
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
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 *  z80.c - Implementació de 'Z80.h'.
 *
 *  NOTA: Els registres que són més grans del que toca han de tindre
 *  els bits sobrants sempre a 0.
 *
 */


#include "Z80.h"




/**********/
/* MACROS */
/**********/

#define SAVE(VAR)                                               \
  if ( fwrite ( &(VAR), sizeof(VAR), 1, f ) != 1 ) return -1

#define LOAD(VAR)                                               \
  if ( fread ( &(VAR), sizeof(VAR), 1, f ) != 1 ) return -1


#define NMI 0x1
#define IRQ 0x2


#define SFLAG 0x80
#define ZFLAG 0x40
#define HFLAG 0x10
#define PVFLAG 0x04
#define NFLAG 0x02
#define CFLAG 0x01


#define MAKE_R _regs.R= (_regs.R&0x80) | (_regs.RAUX&0x7F)


#define R16(HI,LO) ((((Z80u16) _regs.HI)<<8)|_regs.LO)
#define Id(R) (_regs.R+(Z80s8)Z80_read ( _regs.PC++ ))
#define IXd Id(IX)
#define IYd Id(IY)
#define GET_NN(ADDR)        			\
  ADDR= Z80_read ( _regs.PC++ );        	\
  ADDR|= ((Z80u16) Z80_read ( _regs.PC++ ))<<8
#define RESET_FLAGS(MASK) _regs.F&= ((MASK)^0xff)
#define EXRU8(R1,R2,VARU8)    \
  VARU8= (Z80u8) _regs.R1;    \
  _regs.R1= (Z80u8) _regs.R2; \
  _regs.R2= (Z80u8) VARU8
#define EXRU8MEM(R,ADDR,VARU8)        		\
  VARU8= Z80_read ( ADDR );        		\
  Z80_write ( ADDR, (Z80u8) _regs.R );        	\
  _regs.R= VARU8
#define LD_pR16_pR16(H1,L1,H2,L2)        			\
  Z80_write ( R16 ( H1, L1 ), Z80_read ( R16 ( H2, L2 ) ) )
#define INCR16(HI,LO) if ( ++_regs.LO == 0x00 ) ++_regs.HI
#define DECR16(HI,LO) if ( --_regs.LO == 0xff ) --_regs.HI
#define C2(VAL) (Z80u8) -((Z80s8) (VAL))
#define C1(VAL) ((Z80u8)(~(VAL)))
#define ADSB_A_SETFLAGS(VAL,VARU8)        				\
  (((Z80u8) _regs.A)&SFLAG) /* SFLAG és 0x80 */ |        		\
  (((~(VARU8^(VAL)))&(VARU8^_regs.A)&0x80)>>5) /* PVFLAG és 0x04*/
#define AD_A_SETFLAGS(VAL,VARU8)        			\
    _regs.F|= ((_regs.A&0x100)>>8) /* CFLAG és 0x01 */ |        \
      (((VARU8^(VAL))^_regs.A)&HFLAG) /* HFLAG és 0x10 */ |        \
      ADSB_A_SETFLAGS(VAL,VARU8);        			\
    if ( (_regs.A&= 0xff) == 0 ) _regs.F|= ZFLAG
#define SB_A_SETFLAGS(VAL,VARU8)        				\
    _regs.F|= (((~_regs.A)&0x100)>>8) /* CFLAG és 0x01 */ |        	\
      ((~((VARU8^(VAL))^_regs.A))&HFLAG) /* HFLAG és 0x10 */ |        	\
      ADSB_A_SETFLAGS(VAL,VARU8) |        				\
      NFLAG;        							\
    if ( (_regs.A&= 0xff) == 0 ) _regs.F|= ZFLAG
#define ADD_A_VAL(VAL,VARU8)        					\
  VARU8= (Z80u8) _regs.A;        					\
  _regs.A+= (VAL);        				                \
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG );        		\
  AD_A_SETFLAGS(VAL,VARU8)
#define SUB_A_VAL(VAL,VARU8)        					\
  VARU8= (Z80u8) _regs.A;        					\
  _regs.A+= C1(VAL);        				                \
  ++_regs.A;        					                \
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|CFLAG );        		\
  SB_A_SETFLAGS ( C1(VAL), VARU8 )
#define ADC_A_VAL(VAL,VARU8)        					\
  VARU8= (Z80u8) _regs.A;        					\
  _regs.A+= (VAL) + (Z80u16) (_regs.F&CFLAG);        			\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG );        		\
  AD_A_SETFLAGS ( VAL, VARU8 )

#define SBC_A_VAL(VAL,VARU8)        					\
  VARU8= (Z80u8) _regs.A;        					\
  _regs.A+= C1(VAL) + (Z80u16) ((~_regs.F)&CFLAG);                        \
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|CFLAG );        		\
  SB_A_SETFLAGS ( C1(VAL), VARU8 )
#define LOP_A_VAL(OP,VAL)        					\
  _regs.A OP ## = (VAL);        					\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG );        		\
  _regs.F|= (_regs.A?0:ZFLAG) | (((Z80u8) _regs.A)&SFLAG) |        	\
    _calc_pflag[_regs.A]
#define AND_A_VAL(VAL) LOP_A_VAL ( &, VAL ) | HFLAG
#define OR_A_VAL(VAL) LOP_A_VAL ( |, VAL )
#define XOR_A_VAL(VAL) LOP_A_VAL ( ^, VAL )
#define CP_A_VAL(VAL,VARU16)        					\
  VARU16= _regs.A + C1(VAL);        					\
  ++VARU16;        							\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|CFLAG );        		\
  _regs.F|=        							\
    NFLAG | ((~((_regs.A^C1(VAL))^VARU16))&HFLAG) |        		\
    (((~VARU16)&0x100)>>8) | (((Z80u8) VARU16)&SFLAG) |        		\
    (((~(_regs.A^C1(VAL)))&(VARU16^_regs.A)&0x80)>>5) |        		\
    ((VARU16&0xff)?0:ZFLAG)
#define INCDEC_VARU8(VARU8,AUXU8,OP,PVVAL)        	  \
  AUXU8= VARU8;        					  \
  OP VARU8;        					  \
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG );          \
  _regs.F|=        					  \
    (VARU8?0:ZFLAG) | ((AUXU8==PVVAL)?PVFLAG:0) |          \
    (VARU8&SFLAG)
#define INC_VARU8(VARU8,AUXU8)        		  \
  INCDEC_VARU8 ( VARU8, AUXU8, ++, 0x7f ) |          \
  (((0x00^AUXU8)^VARU8)&HFLAG)
#define DEC_VARU8(VARU8,AUXU8)        				\
  INCDEC_VARU8 ( VARU8, AUXU8, --, 0x80 ) |        		\
  ((~((HFLAG^AUXU8)^VARU8))&HFLAG) |        			\
  NFLAG
#define ADD_RU16_RU16(AUX32,A,B)        	\
  AUX32= A+B;        				\
  RESET_FLAGS ( HFLAG|CFLAG|NFLAG );        	\
  _regs.F|=        				\
    ((((A^B)^AUX32)&0x1000)>>8) |        	\
    ((AUX32&0x10000)>>16)
#define ADC_RU16_RU16(AUX32,A,B)        			  \
  AUX32= A+B+(_regs.F&CFLAG);        				  \
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG );        	  \
  _regs.F=        						  \
    ((AUX32&0x8000)>>8) |        				  \
    ((AUX32&0xFFFF)?0:ZFLAG) |        				  \
    ((((A^B)^AUX32)&0x1000)>>8) |        			  \
    (((~(A^B))&(A^AUX32)&0x8000)>>13) |        			  \
    ((AUX32&0x10000)>>16)
#define C1_16(VAL) ((Z80u16) ~(VAL))
#define SBC_RU16_RU16(AUX32,A,B)        	                  \
  AUX32= A+C1_16(B)+((~_regs.F)&CFLAG);        			  \
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|CFLAG );        	  \
  _regs.F|=        				                  \
    NFLAG |        						  \
    ((AUX32&0x8000)>>8) |        				  \
    ((AUX32&0xFFFF)?0:ZFLAG) |        				  \
    (((~((A^C1_16(B))^AUX32))&0x1000)>>8) |        		  \
    (((~(A^C1_16(B)))&(A^AUX32)&0x8000)>>13) |        		  \
    (((~AUX32)&0x10000)>>16)
#define ROTATE_SET_FLAGS(VAR,C)        					\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG );        		\
  _regs.F|= ((VAR)&SFLAG) | _calc_pflag[(VAR)] | ((VAR)?0x00:ZFLAG) | (C)
#define RLC_VARU8(AUX,VAR)        			\
  AUX= (VAR)>>7;        				\
  (VAR)= ((VAR)<<1)|AUX;        		        \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define RL_VARU8(AUX,VAR)               \
  AUX= (VAR)>>7;        	       \
  (VAR) = ((VAR)<<1)|(_regs.F&CFLAG);  \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define RRC_VARU8(AUX,VAR)     \
  AUX= (VAR)&0x1;               \
  (VAR)= ((VAR)>>1)|(AUX<<7);  \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define RR_VARU8(AUX,VAR)        	  \
  AUX= (VAR)&0x1;        		  \
  (VAR)= ((VAR)>>1)|((_regs.F&CFLAG)<<7); \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define SHIFT_SET_FLAGS(VAR)        					\
  _regs.F|= ((VAR)&SFLAG) | _calc_pflag[(VAR)] | ((VAR)?0x00:ZFLAG)
#define SLA_VARU8(VAR)        				\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG ); \
  _regs.F|= (VAR)>>7;                                   \
  (VAR)<<= 1;                                           \
  SHIFT_SET_FLAGS ( VAR )
#define SRA_VARU8(VAR)        				\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG ); \
  _regs.F|= (VAR)&0x1;        				\
  (VAR)= ((VAR)&0x80)|((VAR)>>1);                       \
  SHIFT_SET_FLAGS ( VAR )
#define SRL_VARU8(VAR)        				\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG|CFLAG ); \
  _regs.F|= (VAR)&0x1;        				\
  (VAR)>>= 1;                                           \
  SHIFT_SET_FLAGS ( VAR )
#define RLRD_SET_FLAGS        						\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG );        		\
  _regs.F|= (_regs.A&SFLAG) | _calc_pflag[_regs.A] | (_regs.A?0x00:ZFLAG)
#define BRANCH _regs.PC+= ((Z80s8) Z80_read ( _regs.PC ))+1
#define PUSH_PC        					\
  Z80_write ( --_regs.SP, (Z80u8) (_regs.PC>>8) );        \
  Z80_write ( --_regs.SP, (Z80u8) (_regs.PC&0xff) )


#define LD_R_R return 4
#define LD_R1_R2(R1,R2) _regs.R1= _regs.R2; return 4
#define LD_R_A(R) _regs.R= (Z80u8) _regs.A; return 4
#define LD_IL_A(R) _regs.R= (_regs.R&0xFF00)|((Z80u8) _regs.A); return 4
#define LD_IL_R(R,I) _regs.I= (_regs.I&0xFF00)|((Z80u16) _regs.R); return 4
#define LD_IH_A(R) _regs.R= (_regs.R&0x00FF)|(_regs.A<<8); return 4
#define LD_IH_R(R,I) _regs.I= (_regs.I&0x00FF)|(((Z80u16)_regs.R)<<8); return 4
#define LD_R_IL(R,I) _regs.R= (Z80u8) (_regs.I&0x00FF); return 4
#define LD_R_IH(R,I) _regs.R= (Z80u8) (_regs.I>>8); return 4
#define LD_A_IL(R) _regs.A= (Z80u8) (_regs.R&0x00FF); return 4
#define LD_A_IH(R) _regs.A= (Z80u8) (_regs.R>>8); return 4
#define LD_R_N(R) _regs.R= Z80_read ( _regs.PC++ ); return 7
#define LD_IL_N(R)        				  \
  _regs.R= (_regs.R&0xFF00)|Z80_read ( _regs.PC++ );          \
  return 7
#define LD_IH_N(R)        				  \
  _regs.R= (_regs.R&0x00FF)|(Z80_read ( _regs.PC++ )<<8); \
  return 7
#define LD_R_pHL(R) _regs.R= Z80_read ( R16 ( H, L ) ); return 7
#define LD_R_pIXd(R) _regs.R= Z80_read ( IXd ); return 19
#define LD_R_pIYd(R) _regs.R= Z80_read ( IYd ); return 19
#define LD_pHL_R(R) Z80_write ( R16 ( H, L ), (Z80u8) _regs.R ); return 7
#define LD_pIXd_R(R) Z80_write ( IXd, (Z80u8) _regs.R ); return 19
#define LD_pIYd_R(R) Z80_write ( IYd, (Z80u8) _regs.R ); return 19
#define LD_pId_N(R)        			\
  Z80u16 addr;        				\
  addr= Id(R);        				\
  Z80_write ( addr, Z80_read ( _regs.PC++ ) );        \
  return 19
#define LD_A_pR16(HI,LO) _regs.A= Z80_read ( R16 ( HI, LO ) ); return 7
#define LD_pR16_A(HI,LO) Z80_write ( R16 ( HI, LO ), (Z80u8) _regs.A ); return 7
#define LD_A_IORR(R)        				\
  _regs.A= _regs.R;        				\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG );        \
  _regs.F|= (_regs.R?0:ZFLAG) | (_regs.R&SFLAG) | (_regs.IFF2?PVFLAG:0); \
  return 9
#define LD_IORR_A(R) _regs.R= (Z80u8) _regs.A; return 9

#define LD_DD_NN(HI,LO)        	     \
  _regs.LO= Z80_read ( _regs.PC++ ); \
  _regs.HI= Z80_read ( _regs.PC++ ); \
  return 10
#define LD_RU16_NN_NORET(RU16)        			\
  _regs.RU16= Z80_read ( _regs.PC++ );        		\
  _regs.RU16|= ((Z80u16) Z80_read ( _regs.PC++ ))<<8
#define LD_I_NN(R) LD_RU16_NN_NORET ( R ); return 14
#define LD_DD_pNN_NORET(HI,LO)        		\
  Z80u16 addr;        				\
  GET_NN ( addr );        			\
  _regs.LO= Z80_read ( addr );        		\
  _regs.HI= Z80_read ( addr+1 )
#define LD_DD_pNN_SLOW(HI,LO) LD_DD_pNN_NORET ( HI, LO ); return 20
#define LD_RU16_pNN(RU16)        			\
  Z80u16 addr;        					\
  GET_NN ( addr );        				\
  _regs.RU16= Z80_read ( addr );        		\
  _regs.RU16|= ((Z80u16) Z80_read ( addr+1 ))<<8;        \
  return 20
#define LD_pNN_DD_NORET(HI,LO)        			\
  Z80u16 addr;        					\
  GET_NN ( addr );        				\
  Z80_write ( addr, _regs.LO );        			\
  Z80_write ( addr+1, _regs.HI )
#define LD_pNN_DD_SLOW(HI,LO) LD_pNN_DD_NORET ( HI, LO ); return 20
#define LD_pNN_RU16(RU16)        		 \
  Z80u16 addr;        				 \
  GET_NN ( addr );        			 \
  Z80_write ( addr, (Z80u8) (_regs.RU16&0xff) ); \
  Z80_write ( addr+1, (Z80u8) (_regs.RU16>>8) ); \
  return 20
#define LD_SP_I(R) _regs.SP= _regs.R; return 10
#define PUSH_QQ(HI,LO)        		     \
  Z80_write ( --_regs.SP, (Z80u8) _regs.HI); \
  Z80_write ( --_regs.SP, _regs.LO );             \
  return 11
#define PUSH_I(R)        				\
  Z80_write ( --_regs.SP, (Z80u8) (_regs.R>>8) );        \
  Z80_write ( --_regs.SP, (Z80u8) (_regs.R&0xff) );        \
  return 15
#define POP_QQ(HI,LO)        	     \
  _regs.LO= Z80_read ( _regs.SP++ ); \
  _regs.HI= Z80_read ( _regs.SP++ ); \
  return 10
#define POP_I(R)        				\
  _regs.R= Z80_read ( _regs.SP++ );        		\
  _regs.R|= ((Z80u16) Z80_read ( _regs.SP++ ))<<8;        \
  return 14

#define EX_NORET(R1,R2,R1B,R2B,VARU8)        	\
  EXRU8 ( R1, R1B, VARU8 );        		\
  EXRU8 ( R2, R2B, VARU8 )
#define EX_pSP_I(R)                                             \
  Z80u16 aux;        	                                        \
  aux= (Z80u16) Z80_read ( _regs.SP );        			\
  Z80_write ( _regs.SP, (Z80u8) (_regs.R&0xff) );                \
  aux|= ((Z80u16) Z80_read ( _regs.SP+1 ))<<8;                  \
  Z80_write ( _regs.SP+1, (Z80u8) (_regs.R>>8) );        \
  _regs.R= aux;                                                 \
  return 23
#define LDID_NORET_NOSETPV(IORD)        	\
  LD_pR16_pR16 ( D, E, H, L );        		\
  IORD ## R16 ( D, E );        			\
  IORD ## R16 ( H, L );        			\
  DECR16 ( B, C );        			\
  RESET_FLAGS ( HFLAG|PVFLAG|NFLAG )
#define LDID(IORD)        					\
  LDID_NORET_NOSETPV ( IORD );        				\
  if ( _regs.B != 0x00 || _regs.C != 0x00 ) _regs.F|= PVFLAG;        \
  return 16
#define LDIDR(IORD)        		    \
  LDID_NORET_NOSETPV ( IORD );        	    \
  if ( _regs.B != 0x00 || _regs.C != 0x00 ) \
    {        				    \
      _regs.F|= PVFLAG;        		    \
      _regs.PC-= 2;        		    \
      return 21;        		    \
    }        				    \
  return 16
#define CPID_NORET_NOSETPV(IORD)        				\
  Z80u8 aux;        							\
  aux= ((Z80u8) -((Z80s8) Z80_read ( R16 ( H, L ) )));        		\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG );        			\
  _regs.F|= NFLAG | ((~((_regs.A&0xf) + (aux&0xf)))&HFLAG);        	\
  aux+= (Z80u8) _regs.A;        					\
  _regs.F|= (aux?0:ZFLAG) | (aux&SFLAG);        		        \
  IORD ## R16 ( H, L );        						\
  DECR16 ( B, C )
#define CPID(IORD)        					\
  CPID_NORET_NOSETPV ( IORD );        				\
  if ( _regs.B != 0x00 || _regs.C != 0x00 ) _regs.F|= PVFLAG;        \
  return 16
#define CPIDR(IORD)        		    \
  CPID_NORET_NOSETPV ( IORD );        	    \
  if ( _regs.B != 0x00 || _regs.C != 0x00 ) \
    {        				    \
      _regs.F|= PVFLAG;        		    \
      if ( aux )        		    \
        {				    \
          _regs.PC-= 2;			    \
          return 21;			    \
        }				    \
    }        				    \
  return 16


#define OPVAR_A_R(R,OP)        			\
  Z80u8 aux;        				\
  OP ## _A_VAL ( _regs.R, aux );        	\
  return 4
#define OPVAR_A_A(OP)        			\
  Z80u8 aux;        				\
  OP ## _A_VAL ( aux, aux );        		\
  return 4
#define OPVAR_A_N(OP)        			\
  Z80u8 aux, val;        			\
  val= Z80_read ( _regs.PC++ );        		\
  OP ## _A_VAL ( val, aux );        		\
  return 7
#define OPVAR_A_pHL(OP)        			\
  Z80u8 aux, val;        			\
  val= Z80_read ( R16 ( H, L ) );        	\
  OP ## _A_VAL ( val, aux );        		\
  return 7
#define OPVAR_A_pId(OP,IR)        		\
  Z80u8 aux, val;        			\
  val= Z80_read ( Id(IR) );        		\
  OP ## _A_VAL ( val, aux );        		\
  return 19
#define OPVAR_A_IdL(IR,OP)        			\
  Z80u8 aux,val;        				\
  val= ((Z80u8) (_regs.IR&0xFF));        		\
  OP ## _A_VAL ( val, aux );        			\
  return 4
#define OPVAR_A_IdH(IR,OP)        			\
  Z80u8 aux,val;        				\
  val= ((Z80u8) (_regs.IR>>8));        			\
  OP ## _A_VAL ( val, aux );        			\
  return 4
#define OP_A_R(R,OP)        			\
  OP ## _A_VAL ( (Z80u8) _regs.R );        	\
  return 4
#define OP_A_N(OP)        			\
  Z80u8 val;        				\
  val= Z80_read ( _regs.PC++ );        		\
  OP ## _A_VAL ( val );        			\
  return 7
#define OP_A_pHL(OP)        			\
  Z80u8 val;        				\
  val= Z80_read ( R16 ( H, L ) );        	\
  OP ## _A_VAL ( val );        			\
  return 7
#define OP_A_pId(OP,IR)        			\
  Z80u8 val;        				\
  val= Z80_read ( Id(IR) );        		\
  OP ## _A_VAL ( val );        			\
  return 19
#define OP_A_IL(OP,IR)        			\
  OP ## _A_VAL ( ((Z80u8) (_regs.IR&0xff)) );        \
  return 8 /* 4 més per IX ???*/
#define OP_A_IH(OP,IR)        			\
  OP ## _A_VAL ( ((Z80u8) (_regs.IR>>8)) );        \
  return 8 /* 4 més per IX ???*/
#define CP_A_R(R)        			\
  Z80u16 aux;        				\
  CP_A_VAL ( (Z80u8) _regs.R, aux );        	\
  return 4
#define CP_A_N        				\
  Z80u8 val;        				\
  Z80u16 aux;        				\
  val= Z80_read ( _regs.PC++ );        		\
  CP_A_VAL ( val, aux );        		\
  return 7
#define CP_A_pHL        			\
  Z80u8 val;        				\
  Z80u16 aux;        				\
  val= Z80_read ( R16 ( H, L ) );        	\
  CP_A_VAL ( val, aux );        		\
  return 7
#define CP_A_pId(IR)        			\
  Z80u8 val;        				\
  Z80u16 aux;        				\
  val= Z80_read ( Id(IR) );        		\
  CP_A_VAL ( val, aux );        		\
  return 19
#define CP_A_IL(IR)        			\
  Z80u16 aux;        				\
  CP_A_VAL ( ((Z80u8) (_regs.IR&0xff)), aux );        \
  return 8 /* 4 més per IX ??? */
#define CP_A_IH(IR)        			\
  Z80u16 aux;        				\
  CP_A_VAL ( ((Z80u8) (_regs.IR>>8)), aux );        \
  return 8 /* 4 més per IX ??? */
#define INCDEC_R(R,OP)        			\
  Z80u8 aux;        				\
  OP ## _VARU8 ( _regs.R, aux );        	\
  return 4
#define INCDEC_IdL(R,OP)        		\
  Z80u8 aux, L;        				\
  L= (Z80u8) (_regs.R&0xff);        		\
  OP ## _VARU8 ( L, aux );        		\
  _regs.R= (_regs.R&0xff00)|L;        		\
  return 8 /* 4 + 4 per FD. */
#define INCDEC_IdH(R,OP)        		\
  Z80u8 aux, H;        				\
  H= (Z80u8) (_regs.R>>8);        		\
  OP ## _VARU8 ( H, aux );        		\
  _regs.R= (_regs.R&0x00ff)|(((Z80u16) H)<<8);        \
  return 8 /* 4 + 4 per FD. */
#define INCDEC_A(OP)        			\
  Z80u8 val, aux;        			\
  val= (Z80u8) _regs.A;        			\
  OP ## _VARU8 ( val, aux );                        \
  _regs.A= val;        		                \
  return 4
#define INCDEC_pHL(OP)        			\
  Z80u8 val, aux;        			\
  Z80u16 addr;        				\
  addr= R16 ( H, L );        			\
  val= Z80_read ( addr );        		\
  OP ## _VARU8 ( val, aux );        		\
  Z80_write ( addr, val );        		\
  return 11
#define INCDEC_pId(IR,OP)    \
  Z80u8 val, aux;             \
  Z80u16 addr;        	     \
  addr= Id ( IR );             \
  val= Z80_read ( addr );    \
  OP ## _VARU8 ( val, aux ); \
  Z80_write ( addr, val );   \
  return 23


#define OP_HL_SS_NORET(OP,HI,LO)        	\
  Z80u32 aux;        				\
  Z80u16 a, b;        				\
  a= R16 ( H, L );        			\
  b= R16 ( HI, LO );        			\
  OP ## _RU16_RU16 ( aux, a, b );        	\
  _regs.H= (aux>>8)&0xff;        		\
  _regs.L= aux&0xff
#define OP_HL_RU16_NORET(OP,R)        		\
  Z80u32 aux;        				\
  Z80u16 a;        				\
  a= R16 ( H, L );        			\
  OP ## _RU16_RU16 ( aux, a, _regs.R );        	\
  _regs.H= (aux>>8)&0xff;        		\
  _regs.L= aux&0xff
#define ADD_I_PP(IR,HI,LO)        		\
  Z80u32 aux;        				\
  Z80u16 b;        				\
  b= R16 ( HI, LO );        			\
  ADD_RU16_RU16 ( aux, _regs.IR, b );        	\
  _regs.IR= aux&0xffff;        			\
  return 15
#define ADD_I_RU16(IR,R)        	    \
  Z80u32 aux;        			    \
  ADD_RU16_RU16 ( aux, _regs.IR, _regs.R ); \
  _regs.IR= aux&0xffff;        		    \
  return 15
#define INCDEC_SS(OP,HI,LO)        		\
  OP ## R16 ( HI, LO );        			\
  return 6


#define ROT_R(OP,R)        	 \
  Z80u8 aux;        		 \
  OP ## _VARU8 ( aux, _regs.R ); \
  return 8
#define ROT_A(OP)             \
  Z80u8 aux, var;             \
  var= (Z80u8) _regs.A;             \
  OP ## _VARU8 ( aux, var ); \
  _regs.A= var;              \
  return 8
#define ROT_pHL(OP)             \
  Z80u16 addr;        	     \
  Z80u8 aux, var;             \
  addr= R16 ( H, L );             \
  var= Z80_read ( addr );    \
  OP ## _VARU8 ( aux, var ); \
  Z80_write ( addr, var );   \
  return 15
#define ROT_pId(OP,IR)             \
  Z80u16 addr;        	     \
  Z80u8 aux, var;             \
  addr= _regs.IR + desp;     \
  var= Z80_read ( addr );    \
  OP ## _VARU8 ( aux, var ); \
  Z80_write ( addr, var );   \
  return 23
#define SHI_R(OP,R)        	 \
  OP ## _VARU8 ( _regs.R );         \
  return 8
#define SHI_A(OP)             \
  Z80u8 var;        	     \
  var= (Z80u8) _regs.A;             \
  OP ## _VARU8 ( var );      \
  _regs.A= var;              \
  return 8
#define SHI_pHL(OP)             \
  Z80u16 addr;        	     \
  Z80u8 var;        	     \
  addr= R16 ( H, L );             \
  var= Z80_read ( addr );    \
  OP ## _VARU8 ( var );      \
  Z80_write ( addr, var );   \
  return 15
#define SHI_pId(OP,IR)             \
  Z80u16 addr;        	     \
  Z80u8 var;        	     \
  addr= _regs.IR + desp;     \
  var= Z80_read ( addr );    \
  OP ## _VARU8 ( var );             \
  Z80_write ( addr, var );   \
  return 23


#define BIT_VARU8_NORET(VAR,MASK)        	\
  RESET_FLAGS ( ZFLAG|NFLAG );        		\
  _regs.F|= HFLAG|(((VAR)&(MASK))?0x00:ZFLAG)
#define BIT_R(R,MASK)        			\
  BIT_VARU8_NORET ( _regs.R, MASK );        	\
  return 8
#define BIT_pHL(MASK)        	  \
  Z80u8 val;        		  \
  val= Z80_read ( R16 ( H, L ) ); \
  BIT_VARU8_NORET ( val, MASK );  \
  return 12
#define BIT_pId(IR,MASK)        		\
  Z80u8 val;        				\
  val= Z80_read ( (Z80u16) (_regs.IR+desp) );        \
  BIT_VARU8_NORET ( val, MASK );        	\
  return 20
#define SET_R(R,MASK) _regs.R|= (MASK); return 8
#define SET_pHL(MASK)        			  \
  Z80u16 addr;        				  \
  addr= R16 ( H, L );        			  \
  Z80_write ( addr, Z80_read ( addr ) | (MASK) ); \
  return 15
#define SET_pId(IR,MASK)        		  \
  Z80u16 addr;        				  \
  addr= _regs.IR + desp;        		  \
  Z80_write ( addr, Z80_read ( addr ) | (MASK) ); \
  return 23
#define RES_R(R,MASK) _regs.R&= (MASK); return 8
#define RES_pHL(MASK)        			  \
  Z80u16 addr;        				  \
  addr= R16 ( H, L );        			  \
  Z80_write ( addr, Z80_read ( addr ) & (MASK) ); \
  return 15
#define RES_pId(IR,MASK)        		  \
  Z80u16 addr;        				  \
  addr= _regs.IR + desp;        		  \
  Z80_write ( addr, Z80_read ( addr ) & (MASK) ); \
  return 23


#define JP_GET_ADDR() \
  Z80u16 addr;              \
  GET_NN ( addr )
#define JP()           \
  JP_GET_ADDR ();  \
  _regs.PC= addr;  \
  return 10
#define JP_COND(COND)        			\
  JP_GET_ADDR ();        			\
  if ( (COND) ) _regs.PC= addr;        		\
  return 10
#define JR_COND(COND)        	       \
  if ( (COND) ) { BRANCH; return 12; } \
  else { ++_regs.PC; return 7; }


#define CALL_NORET(VARU16)        			\
  GET_NN ( VARU16 );        				\
  PUSH_PC;        					\
  _regs.PC= VARU16
#define CALL_COND(COND)        			   \
  Z80u16 aux;        				   \
  if ( (COND) ) { CALL_NORET ( aux ); return 17; } \
  else { _regs.PC+= 2; return 10; }
#define RET_NORET        				\
  _regs.PC= Z80_read ( _regs.SP++ );        		\
  _regs.PC|= ((Z80u16) Z80_read ( _regs.SP++ ))<<8
#define RET_COND(COND)        			\
  if ( (COND) ) { RET_NORET; return 11; }        \
  else return 5
#define RST_P(P) PUSH_PC; _regs.PC= P; _regs.unhalted= Z80_TRUE; return 11


#define IN_C_SETFLAGS(VAR)        					\
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|NFLAG );        		\
  _regs.F|= (VAR&SFLAG) | (VAR?0x00:ZFLAG) | _calc_pflag[VAR]
#define IN_R_C(R)        						\
  _regs.R= Z80_io_read ( _regs.C );        				\
  IN_C_SETFLAGS ( _regs.R );        					\
  return 12
#define INID_NORET_NOSET(IORD)        		       \
  Z80_write ( R16 ( H, L ), Z80_io_read ( _regs.C ) ); \
  --_regs.B;        				       \
  IORD ## R16 ( H, L )
#define INID(IORD)        		  \
  INID_NORET_NOSET ( IORD );        	  \
  RESET_FLAGS ( ZFLAG );        	  \
  _regs.F|= NFLAG | (_regs.B?0x00:ZFLAG); \
  return 16
#define INIDR(IORD)        	   \
  INID_NORET_NOSET ( IORD );           \
  if ( _regs.B )        	   \
    {        			   \
      RESET_FLAGS ( ZFLAG );           \
      _regs.F|= NFLAG;        	   \
      _regs.PC-= 2;        	   \
      return 21;        	   \
    }        			   \
  _regs.F|= ZFLAG|NFLAG;           \
  return 16
#define OUT_C_R(R)        				\
  Z80_io_write ( _regs.C, (Z80u8) _regs.R );        	\
  return 12
#define OUTID_NORET_NOSET(IORD) \
  Z80_io_write ( _regs.C, Z80_read ( R16 ( H, L ) ) ); \
  --_regs.B;        				       \
  IORD ## R16 ( H, L )
#define OUTID(IORD)        		  \
  OUTID_NORET_NOSET ( IORD );        	  \
  RESET_FLAGS ( ZFLAG );        	  \
  _regs.F|= NFLAG | (_regs.B?0x00:ZFLAG); \
  return 16
#define OUTIDR(IORD)        		  \
  OUTID_NORET_NOSET ( IORD );        	  \
  if ( _regs.B )        		  \
    {        				  \
      RESET_FLAGS ( ZFLAG );        	  \
      _regs.F|= NFLAG;        		  \
      _regs.PC-= 2;        		  \
      return 21;        		  \
    }        				  \
  _regs.F|= ZFLAG|NFLAG;        	  \
  return 16




/************/
/* CONSTANS */
/************/

static const Z80u16 _daa_table[2048]=
  {
     0x4400, 0x0001, 0x0002, 0x0403, 0x0004, 0x0405, 0x0406, 0x0007,
     0x0008, 0x0409, 0x1010, 0x1411, 0x1412, 0x1013, 0x1414, 0x1015,
     0x0010, 0x0411, 0x0412, 0x0013, 0x0414, 0x0015, 0x0016, 0x0417,
     0x0418, 0x0019, 0x1020, 0x1421, 0x1422, 0x1023, 0x1424, 0x1025,
     0x0020, 0x0421, 0x0422, 0x0023, 0x0424, 0x0025, 0x0026, 0x0427,
     0x0428, 0x0029, 0x1430, 0x1031, 0x1032, 0x1433, 0x1034, 0x1435,
     0x0430, 0x0031, 0x0032, 0x0433, 0x0034, 0x0435, 0x0436, 0x0037,
     0x0038, 0x0439, 0x1040, 0x1441, 0x1442, 0x1043, 0x1444, 0x1045,
     0x0040, 0x0441, 0x0442, 0x0043, 0x0444, 0x0045, 0x0046, 0x0447,
     0x0448, 0x0049, 0x1450, 0x1051, 0x1052, 0x1453, 0x1054, 0x1455,
     0x0450, 0x0051, 0x0052, 0x0453, 0x0054, 0x0455, 0x0456, 0x0057,
     0x0058, 0x0459, 0x1460, 0x1061, 0x1062, 0x1463, 0x1064, 0x1465,
     0x0460, 0x0061, 0x0062, 0x0463, 0x0064, 0x0465, 0x0466, 0x0067,
     0x0068, 0x0469, 0x1070, 0x1471, 0x1472, 0x1073, 0x1474, 0x1075,
     0x0070, 0x0471, 0x0472, 0x0073, 0x0474, 0x0075, 0x0076, 0x0477,
     0x0478, 0x0079, 0x9080, 0x9481, 0x9482, 0x9083, 0x9484, 0x9085,
     0x8080, 0x8481, 0x8482, 0x8083, 0x8484, 0x8085, 0x8086, 0x8487,
     0x8488, 0x8089, 0x9490, 0x9091, 0x9092, 0x9493, 0x9094, 0x9495,
     0x8490, 0x8091, 0x8092, 0x8493, 0x8094, 0x8495, 0x8496, 0x8097,
     0x8098, 0x8499, 0x5500, 0x1101, 0x1102, 0x1503, 0x1104, 0x1505,
     0x4500, 0x0101, 0x0102, 0x0503, 0x0104, 0x0505, 0x0506, 0x0107,
     0x0108, 0x0509, 0x1110, 0x1511, 0x1512, 0x1113, 0x1514, 0x1115,
     0x0110, 0x0511, 0x0512, 0x0113, 0x0514, 0x0115, 0x0116, 0x0517,
     0x0518, 0x0119, 0x1120, 0x1521, 0x1522, 0x1123, 0x1524, 0x1125,
     0x0120, 0x0521, 0x0522, 0x0123, 0x0524, 0x0125, 0x0126, 0x0527,
     0x0528, 0x0129, 0x1530, 0x1131, 0x1132, 0x1533, 0x1134, 0x1535,
     0x0530, 0x0131, 0x0132, 0x0533, 0x0134, 0x0535, 0x0536, 0x0137,
     0x0138, 0x0539, 0x1140, 0x1541, 0x1542, 0x1143, 0x1544, 0x1145,
     0x0140, 0x0541, 0x0542, 0x0143, 0x0544, 0x0145, 0x0146, 0x0547,
     0x0548, 0x0149, 0x1550, 0x1151, 0x1152, 0x1553, 0x1154, 0x1555,
     0x0550, 0x0151, 0x0152, 0x0553, 0x0154, 0x0555, 0x0556, 0x0157,
     0x0158, 0x0559, 0x1560, 0x1161, 0x1162, 0x1563, 0x1164, 0x1565,
     0x0560, 0x0161, 0x0162, 0x0563, 0x0164, 0x0565, 0x0566, 0x0167,
     0x0168, 0x0569, 0x1170, 0x1571, 0x1572, 0x1173, 0x1574, 0x1175,
     0x0170, 0x0571, 0x0572, 0x0173, 0x0574, 0x0175, 0x0176, 0x0577,
     0x0578, 0x0179, 0x9180, 0x9581, 0x9582, 0x9183, 0x9584, 0x9185,
     0x8180, 0x8581, 0x8582, 0x8183, 0x8584, 0x8185, 0x8186, 0x8587,
     0x8588, 0x8189, 0x9590, 0x9191, 0x9192, 0x9593, 0x9194, 0x9595,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x0001, 0x0002, 0x0403, 0x0004, 0x0405, 0x0406, 0x0007,
     0x0008, 0x0409, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0010, 0x0411, 0x0412, 0x0013, 0x0414, 0x0015, 0x0016, 0x0417,
     0x0418, 0x0019, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0020, 0x0421, 0x0422, 0x0023, 0x0424, 0x0025, 0x0026, 0x0427,
     0x0428, 0x0029, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0430, 0x0031, 0x0032, 0x0433, 0x0034, 0x0435, 0x0436, 0x0037,
     0x0038, 0x0439, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0040, 0x0441, 0x0442, 0x0043, 0x0444, 0x0045, 0x0046, 0x0447,
     0x0448, 0x0049, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0450, 0x0051, 0x0052, 0x0453, 0x0054, 0x0455, 0x0456, 0x0057,
     0x0058, 0x0459, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0460, 0x0061, 0x0062, 0x0463, 0x0064, 0x0465, 0x0466, 0x0067,
     0x0068, 0x0469, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0070, 0x0471, 0x0472, 0x0073, 0x0474, 0x0075, 0x0076, 0x0477,
     0x0478, 0x0079, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8080, 0x8481, 0x8482, 0x8083, 0x8484, 0x8085, 0x8086, 0x8487,
     0x8488, 0x8089, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8490, 0x8091, 0x8092, 0x8493, 0x8094, 0x8495, 0x8496, 0x8097,
     0x8098, 0x8499, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0110, 0x0511, 0x0512, 0x0113, 0x0514, 0x0115, 0x0116, 0x0517,
     0x0518, 0x0119, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0120, 0x0521, 0x0522, 0x0123, 0x0524, 0x0125, 0x0126, 0x0527,
     0x0528, 0x0129, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0530, 0x0131, 0x0132, 0x0533, 0x0134, 0x0535, 0x0536, 0x0137,
     0x0138, 0x0539, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0140, 0x0541, 0x0542, 0x0143, 0x0544, 0x0145, 0x0146, 0x0547,
     0x0548, 0x0149, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0550, 0x0151, 0x0152, 0x0553, 0x0154, 0x0555, 0x0556, 0x0157,
     0x0158, 0x0559, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0560, 0x0161, 0x0162, 0x0563, 0x0164, 0x0565, 0x0566, 0x0167,
     0x0168, 0x0569, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0170, 0x0571, 0x0572, 0x0173, 0x0574, 0x0175, 0x0176, 0x0577,
     0x0578, 0x0179, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8180, 0x8581, 0x8582, 0x8183, 0x8584, 0x8185, 0x8186, 0x8587,
     0x8588, 0x8189, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8590, 0x8191, 0x8192, 0x8593, 0x8194, 0x8595, 0x8596, 0x8197,
     0x8198, 0x8599, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0406, 0x0007, 0x0008, 0x0409, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0016, 0x0417, 0x0418, 0x0019, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0026, 0x0427, 0x0428, 0x0029, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0436, 0x0037, 0x0038, 0x0439, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0046, 0x0447, 0x0448, 0x0049, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0456, 0x0057, 0x0058, 0x0459, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0466, 0x0067, 0x0068, 0x0469, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0076, 0x0477, 0x0478, 0x0079, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8086, 0x8487, 0x8488, 0x8089, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8496, 0x8097, 0x8098, 0x8499, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0506, 0x0107, 0x0108, 0x0509, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0116, 0x0517, 0x0518, 0x0119, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0126, 0x0527, 0x0528, 0x0129, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0536, 0x0137, 0x0138, 0x0539, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0146, 0x0547, 0x0548, 0x0149, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0556, 0x0157, 0x0158, 0x0559, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0566, 0x0167, 0x0168, 0x0569, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x0176, 0x0577, 0x0578, 0x0179, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8186, 0x8587, 0x8588, 0x8189, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x8596, 0x8197, 0x8198, 0x8599, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x5400, 0x1001,
     0x1002, 0x1403, 0x1004, 0x1405, 0x1406, 0x1007, 0x1008, 0x1409,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1010, 0x1411,
     0x1412, 0x1013, 0x1414, 0x1015, 0x1016, 0x1417, 0x1418, 0x1019,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1020, 0x1421,
     0x1422, 0x1023, 0x1424, 0x1025, 0x1026, 0x1427, 0x1428, 0x1029,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1430, 0x1031,
     0x1032, 0x1433, 0x1034, 0x1435, 0x1436, 0x1037, 0x1038, 0x1439,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1040, 0x1441,
     0x1442, 0x1043, 0x1444, 0x1045, 0x1046, 0x1447, 0x1448, 0x1049,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1450, 0x1051,
     0x1052, 0x1453, 0x1054, 0x1455, 0x1456, 0x1057, 0x1058, 0x1459,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1460, 0x1061,
     0x1062, 0x1463, 0x1064, 0x1465, 0x1466, 0x1067, 0x1068, 0x1469,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1070, 0x1471,
     0x1472, 0x1073, 0x1474, 0x1075, 0x1076, 0x1477, 0x1478, 0x1079,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x9080, 0x9481,
     0x9482, 0x9083, 0x9484, 0x9085, 0x9086, 0x9487, 0x9488, 0x9089,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x5500, 0x1101,
     0x1102, 0x1503, 0x1104, 0x1505, 0x1506, 0x1107, 0x1108, 0x1509,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1110, 0x1511,
     0x1512, 0x1113, 0x1514, 0x1115, 0x1116, 0x1517, 0x1518, 0x1119,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1120, 0x1521,
     0x1522, 0x1123, 0x1524, 0x1125, 0x1126, 0x1527, 0x1528, 0x1129,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1530, 0x1131,
     0x1132, 0x1533, 0x1134, 0x1535, 0x1536, 0x1137, 0x1138, 0x1539,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1140, 0x1541,
     0x1542, 0x1143, 0x1544, 0x1145, 0x1146, 0x1547, 0x1548, 0x1149,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1550, 0x1151,
     0x1152, 0x1553, 0x1154, 0x1555, 0x1556, 0x1157, 0x1158, 0x1559,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1560, 0x1161,
     0x1162, 0x1563, 0x1164, 0x1565, 0x1566, 0x1167, 0x1168, 0x1569,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x1170, 0x1571,
     0x1572, 0x1173, 0x1574, 0x1175, 0x1176, 0x1577, 0x1578, 0x1179,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x9180, 0x9581,
     0x9582, 0x9183, 0x9584, 0x9185, 0x9186, 0x9587, 0x9588, 0x9189,
     0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x4400, 0x9590, 0x9191,
     0x9192, 0x9593, 0x9194, 0x9595, 0x9596, 0x9197, 0x9198, 0x9599
  };

static const Z80u8 _calc_pflag[256]=
  {
    PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00,
    PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG,
    PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00,
    0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00,
    PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00,
    0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00,
    PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00,
    0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG,
    PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00,
    PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG,
    PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00,
    0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG,
    PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG, PVFLAG, 0x00, PVFLAG, 0x00,
    0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00,
    PVFLAG, 0x00, 0x00, PVFLAG, 0x00, PVFLAG, PVFLAG, 0x00, 0x00, PVFLAG,
    PVFLAG, 0x00, PVFLAG, 0x00, 0x00, PVFLAG
  };




/*********/
/* ESTAT */
/*********/

/* L'acumulador tenen més bits dels que neecessita. */
static struct
{
  
  Z80u16 IX;
  Z80u16 IY;
  Z80u16 SP;
  Z80u16 PC;
  Z80u16 A,A2;
  Z80u8  F,F2;
  Z80u8  B,B2;
  Z80u8  C,C2;
  Z80u8  D,D2;
  Z80u8  E,E2;
  Z80u8  H,H2;
  Z80u8  L,L2;
  Z80u8  I;
  Z80u8  R,RAUX;
  int    imode;
  Z80_Bool IFF1;
  Z80_Bool IFF2;
  Z80_Bool halted;
  Z80_Bool unhalted;
  Z80_Bool enable_IFF;
  
} _regs;


/* Opcode de la instrucció que s'està executant. */
static Z80u8 _opcode;
static Z80u8 _opcode2;
static Z80u8 _opcode3;


/* Informació sobre interrupcions. */
static struct
{
  
  int   flags;    /* 0 indica que no hi han interrupcions. */
  Z80u8 bus;      /* Valor que s'ha ficat en el bus. */
  
} _interruptions;


/* Informació de l'usuari. */
static Z80_Warning *_warning;
static void *_udata;




/****************/
/* INSTRUCCIONS */
/****************/

static int
unk2 (void)
{
  _warning ( _udata, "l'opcode '0x%02x 0x%02x' és desconegut",
             _opcode, _opcode2 );
  return 0;
}


static int
unk3 (
      Z80s8 desp
      )
{
  _warning ( _udata, "l'opcode '0x%02x 0x%02x 0x%02x 0x%02x' és desconegut",
             _opcode, _opcode2, (Z80u8) desp, _opcode3 );
  return 0;
}


/* 8-BIT LOAD GROUP */

static int ld_B_B (void) { LD_R_R; }
static int ld_B_C (void) { LD_R1_R2 ( B, C ); }
static int ld_B_D (void) { LD_R1_R2 ( B, D ); }
static int ld_B_E (void) { LD_R1_R2 ( B, E ); }
static int ld_B_H (void) { LD_R1_R2 ( B, H ); }
static int ld_B_L (void) { LD_R1_R2 ( B, L ); }
static int ld_B_A (void) { LD_R_A ( B ); }
static int ld_C_B (void) { LD_R1_R2 ( C, B ); }
static int ld_C_C (void) { LD_R_R; }
static int ld_C_D (void) { LD_R1_R2 ( C, D ); }
static int ld_C_E (void) { LD_R1_R2 ( C, E ); }
static int ld_C_H (void) { LD_R1_R2 ( C, H ); }
static int ld_C_L (void) { LD_R1_R2 ( C, L ); }
static int ld_C_A (void) { LD_R_A ( C ); }
static int ld_D_B (void) { LD_R1_R2 ( D, B ); }
static int ld_D_C (void) { LD_R1_R2 ( D, C ); }
static int ld_D_D (void) { LD_R_R; }
static int ld_D_E (void) { LD_R1_R2 ( D, E ); }
static int ld_D_H (void) { LD_R1_R2 ( D, H ); }
static int ld_D_L (void) { LD_R1_R2 ( D, L ); }
static int ld_D_A (void) { LD_R_A ( D ); }
static int ld_E_B (void) { LD_R1_R2 ( E, B ); }
static int ld_E_C (void) { LD_R1_R2 ( E, C ); }
static int ld_E_D (void) { LD_R1_R2 ( E, D ); }
static int ld_E_E (void) { LD_R_R; }
static int ld_E_H (void) { LD_R1_R2 ( E, H ); }
static int ld_E_L (void) { LD_R1_R2 ( E, L ); }
static int ld_E_A (void) { LD_R_A ( E ); }
static int ld_H_B (void) { LD_R1_R2 ( H, B ); }
static int ld_H_C (void) { LD_R1_R2 ( H, C ); }
static int ld_H_D (void) { LD_R1_R2 ( H, D ); }
static int ld_H_E (void) { LD_R1_R2 ( H, E ); }
static int ld_H_H (void) { LD_R_R; }
static int ld_H_L (void) { LD_R1_R2 ( H, L ); }
static int ld_H_A (void) { LD_R_A ( H ); }
static int ld_L_B (void) { LD_R1_R2 ( L, B ); }
static int ld_L_C (void) { LD_R1_R2 ( L, C ); }
static int ld_L_D (void) { LD_R1_R2 ( L, D ); }
static int ld_L_E (void) { LD_R1_R2 ( L, E ); }
static int ld_L_H (void) { LD_R1_R2 ( L, H ); }
static int ld_L_L (void) { LD_R_R; }
static int ld_L_A (void) { LD_R_A ( L ); }
static int ld_A_B (void) { LD_R1_R2 ( A, B ); }
static int ld_A_C (void) { LD_R1_R2 ( A, C ); }
static int ld_A_D (void) { LD_R1_R2 ( A, D ); }
static int ld_A_E (void) { LD_R1_R2 ( A, E ); }
static int ld_A_H (void) { LD_R1_R2 ( A, H ); }
static int ld_A_L (void) { LD_R1_R2 ( A, L ); }
static int ld_A_A (void) { LD_R_R; }
static int ld_B_n (void) { LD_R_N ( B ); }
static int ld_C_n (void) { LD_R_N ( C ); }
static int ld_D_n (void) { LD_R_N ( D ); }
static int ld_E_n (void) { LD_R_N ( E ); }
static int ld_H_n (void) { LD_R_N ( H ); }
static int ld_L_n (void) { LD_R_N ( L ); }
static int ld_A_n (void) { LD_R_N ( A ); }
static int ld_B_pHL (void) { LD_R_pHL ( B ); }
static int ld_C_pHL (void) { LD_R_pHL ( C ); }
static int ld_D_pHL (void) { LD_R_pHL ( D ); }
static int ld_E_pHL (void) { LD_R_pHL ( E ); }
static int ld_H_pHL (void) { LD_R_pHL ( H ); }
static int ld_L_pHL (void) { LD_R_pHL ( L ); }
static int ld_A_pHL (void) { LD_R_pHL ( A ); }
static int ld_B_pIXd (void) { LD_R_pIXd ( B ); }
static int ld_C_pIXd (void) { LD_R_pIXd ( C ); }
static int ld_D_pIXd (void) { LD_R_pIXd ( D ); }
static int ld_E_pIXd (void) { LD_R_pIXd ( E ); }
static int ld_H_pIXd (void) { LD_R_pIXd ( H ); }
static int ld_L_pIXd (void) { LD_R_pIXd ( L ); }
static int ld_A_pIXd (void) { LD_R_pIXd ( A ); }
static int ld_B_pIYd (void) { LD_R_pIYd ( B ); }
static int ld_C_pIYd (void) { LD_R_pIYd ( C ); }
static int ld_D_pIYd (void) { LD_R_pIYd ( D ); }
static int ld_E_pIYd (void) { LD_R_pIYd ( E ); }
static int ld_H_pIYd (void) { LD_R_pIYd ( H ); }
static int ld_L_pIYd (void) { LD_R_pIYd ( L ); }
static int ld_A_pIYd (void) { LD_R_pIYd ( A ); }
static int ld_pHL_B (void) { LD_pHL_R ( B ); }
static int ld_pHL_C (void) { LD_pHL_R ( C ); }
static int ld_pHL_D (void) { LD_pHL_R ( D ); }
static int ld_pHL_E (void) { LD_pHL_R ( E ); }
static int ld_pHL_H (void) { LD_pHL_R ( H ); }
static int ld_pHL_L (void) { LD_pHL_R ( L ); }
static int ld_pHL_A (void) { LD_pHL_R ( A ); }
static int ld_pIXd_B (void) { LD_pIXd_R ( B ); }
static int ld_pIXd_C (void) { LD_pIXd_R ( C ); }
static int ld_pIXd_D (void) { LD_pIXd_R ( D ); }
static int ld_pIXd_E (void) { LD_pIXd_R ( E ); }
static int ld_pIXd_H (void) { LD_pIXd_R ( H ); }
static int ld_pIXd_L (void) { LD_pIXd_R ( L ); }
static int ld_pIXd_A (void) { LD_pIXd_R ( A ); }
static int ld_pIYd_B (void) { LD_pIYd_R ( B ); }
static int ld_pIYd_C (void) { LD_pIYd_R ( C ); }
static int ld_pIYd_D (void) { LD_pIYd_R ( D ); }
static int ld_pIYd_E (void) { LD_pIYd_R ( E ); }
static int ld_pIYd_H (void) { LD_pIYd_R ( H ); }
static int ld_pIYd_L (void) { LD_pIYd_R ( L ); }
static int ld_pIYd_A (void) { LD_pIYd_R ( A ); }
static int ld_pHL_n (void)
{
  Z80_write ( R16 ( H, L ), Z80_read ( _regs.PC++ ) );
  return 10;
}
static int ld_pIXd_n (void) { LD_pId_N ( IX ); }
static int ld_pIYd_n (void) { LD_pId_N ( IY ); }
static int ld_A_pBC (void) { LD_A_pR16 ( B, C ); }
static int ld_A_pDE (void) { LD_A_pR16 ( D, E ); }
static int ld_A_pnn (void)
{
  Z80u16 addr;
  GET_NN ( addr );
  _regs.A= Z80_read ( addr );
  return 13;
}
static int ld_pBC_A (void) { LD_pR16_A ( B, C ); }
static int ld_pDE_A (void) { LD_pR16_A ( D, E ); }
static int ld_pnn_A (void)
{
  Z80u16 addr;
  GET_NN ( addr );
  Z80_write ( addr, (Z80u8) _regs.A );
  return 13;
}
static int ld_A_I (void) { LD_A_IORR ( I ); }
static int ld_A_R (void) { MAKE_R; LD_A_IORR ( R ); }
static int ld_I_A (void) { LD_IORR_A ( I ); }
static int ld_R_A (void) { _regs.RAUX= LD_IORR_A ( R ); }
static int ld_IXL_A (void) { LD_IL_A ( IX ); }
static int ld_IXL_B (void) { LD_IL_R ( B, IX ); }
static int ld_IXL_C (void) { LD_IL_R ( C, IX ); }
static int ld_IXL_D (void) { LD_IL_R ( D, IX ); }
static int ld_IXL_E (void) { LD_IL_R ( E, IX ); }
static int ld_IYL_A (void) { LD_IL_A ( IY ); }
static int ld_IYL_B (void) { LD_IL_R ( B, IY ); }
static int ld_IYL_C (void) { LD_IL_R ( C, IY ); }
static int ld_IYL_D (void) { LD_IL_R ( D, IY ); }
static int ld_IYL_E (void) { LD_IL_R ( E, IY ); }
static int ld_IXH_A (void) { LD_IH_A ( IX ); }
static int ld_IXH_B (void) { LD_IH_R ( B, IX ); }
static int ld_IXH_C (void) { LD_IH_R ( C, IX ); }
static int ld_IXH_D (void) { LD_IH_R ( D, IX ); }
static int ld_IXH_E (void) { LD_IH_R ( E, IX ); }
static int ld_IYH_A (void) { LD_IH_A ( IY ); }
static int ld_IYH_B (void) { LD_IH_R ( B, IY ); }
static int ld_IYH_C (void) { LD_IH_R ( C, IY ); }
static int ld_IYH_D (void) { LD_IH_R ( D, IY ); }
static int ld_IYH_E (void) { LD_IH_R ( E, IY ); }
static int ld_B_IXL (void) { LD_R_IL ( B, IX ); }
static int ld_C_IXL (void) { LD_R_IL ( C, IX ); }
static int ld_D_IXL (void) { LD_R_IL ( D, IX ); }
static int ld_E_IXL (void) { LD_R_IL ( E, IX ); }
static int ld_B_IYL (void) { LD_R_IL ( B, IY ); }
static int ld_C_IYL (void) { LD_R_IL ( C, IY ); }
static int ld_D_IYL (void) { LD_R_IL ( D, IY ); }
static int ld_E_IYL (void) { LD_R_IL ( E, IY ); }
static int ld_B_IXH (void) { LD_R_IH ( B, IX ); }
static int ld_C_IXH (void) { LD_R_IH ( C, IX ); }
static int ld_D_IXH (void) { LD_R_IH ( D, IX ); }
static int ld_E_IXH (void) { LD_R_IH ( E, IX ); }
static int ld_B_IYH (void) { LD_R_IH ( B, IY ); }
static int ld_C_IYH (void) { LD_R_IH ( C, IY ); }
static int ld_D_IYH (void) { LD_R_IH ( D, IY ); }
static int ld_E_IYH (void) { LD_R_IH ( E, IY ); }
static int ld_A_IXL (void) { LD_A_IL ( IX ); }
static int ld_A_IYL (void) { LD_A_IL ( IY ); }
static int ld_A_IXH (void) { LD_A_IH ( IX ); }
static int ld_A_IYH (void) { LD_A_IH ( IY ); }
static int ld_IXL_n (void) { LD_IL_N ( IX ); }
static int ld_IYL_n (void) { LD_IL_N ( IY ); }
static int ld_IXH_n (void) { LD_IH_N ( IX ); }
static int ld_IYH_n (void) { LD_IH_N ( IY ); }


/* 16-BIT LOAD GROUP */

static int ld_BC_nn (void) { LD_DD_NN ( B, C ); }
static int ld_DE_nn (void) { LD_DD_NN ( D, E ); }
static int ld_HL_nn (void) { LD_DD_NN ( H, L ); }
static int ld_SP_nn (void) { LD_RU16_NN_NORET ( SP ); return 10; }
static int ld_IX_nn (void) { LD_I_NN ( IX ); }
static int ld_IY_nn (void) { LD_I_NN ( IY ); }
static int ld_HL_pnn (void) { LD_DD_pNN_NORET ( H, L ); return 16; }
static int ld_BC_pnn (void) { LD_DD_pNN_SLOW ( B, C ); }
static int ld_DE_pnn (void) { LD_DD_pNN_SLOW ( D, E ); }
static int ld_HL_pnn_2 (void) { LD_DD_pNN_SLOW ( H, L ); }
static int ld_SP_pnn (void) { LD_RU16_pNN ( SP ); }
static int ld_IX_pnn (void) { LD_RU16_pNN ( IX ); }
static int ld_IY_pnn (void) { LD_RU16_pNN ( IY ); }
static int ld_pnn_HL (void) { LD_pNN_DD_NORET ( H, L ); return 16; }
static int ld_pnn_BC (void) { LD_pNN_DD_SLOW ( B, C ); }
static int ld_pnn_DE (void) { LD_pNN_DD_SLOW ( D, E ); }
static int ld_pnn_HL_2 (void) { LD_pNN_DD_SLOW ( H, L ); }
static int ld_pnn_SP (void) { LD_pNN_RU16 ( SP ); }
static int ld_pnn_IX (void) { LD_pNN_RU16 ( IX ); }
static int ld_pnn_IY (void) { LD_pNN_RU16 ( IY ); }
static int ld_SP_HL (void) { _regs.SP= R16 ( H, L ); return 6; }
static int ld_SP_IX (void) { LD_SP_I ( IX ); }
static int ld_SP_IY (void) { LD_SP_I ( IY ); }
static int push_BC (void) { PUSH_QQ ( B, C ); }
static int push_DE (void) { PUSH_QQ ( D, E ); }
static int push_HL (void) { PUSH_QQ ( H, L ); }
static int push_AF (void) { PUSH_QQ ( A, F ); }
static int push_IX (void) { PUSH_I ( IX ); }
static int push_IY (void) { PUSH_I ( IY ); }
static int pop_BC (void) { POP_QQ ( B, C ); }
static int pop_DE (void) { POP_QQ ( D, E ); }
static int pop_HL (void) { POP_QQ ( H, L ); }
static int pop_AF (void) { POP_QQ ( A, F ); }
static int pop_IX (void) { POP_I ( IX ); }
static int pop_IY (void) { POP_I ( IY ); }


/* EXCHANGE, BLOCK TRANSFER, AND SEARCH GROUP */

static int ex_DE_HL (void)
{
  Z80u8 aux;
  EX_NORET ( D, E, H, L, aux );
  return 4;
}
static int ex_AF_AF2 (void)
{
  Z80u8 aux;
  EX_NORET ( A, F, A2, F2, aux );
  return 4;
}
static int exx (void)
{
  Z80u8 aux;
  EX_NORET ( B, C, B2, C2, aux );
  EX_NORET ( D, E, D2, E2, aux );
  EX_NORET ( H, L, H2, L2, aux );
  return 4;
}
static int ex_pSP_HL (void)
{
  Z80u8 aux;
  EXRU8MEM ( L, _regs.SP, aux );
  EXRU8MEM ( H, _regs.SP+1, aux );
  return 19;
}
static int ex_pSP_IX (void) { EX_pSP_I ( IX ); }
static int ex_pSP_IY (void) { EX_pSP_I ( IY ); }
static int ldi (void) { LDID ( INC ); }
static int ldir (void) { LDIDR ( INC ); }
static int ldd (void) { LDID ( DEC ); }
static int lddr (void) { LDIDR ( DEC ); }
static int cpi (void) { CPID ( INC ); }
static int cpir (void) { CPIDR ( INC ); }
static int cpd (void) { CPID ( DEC ); }
static int cpdr (void) { CPIDR ( DEC ); }


/* 8-BIT ARITHMETIC GROUP */

static int add_A_B (void) { OPVAR_A_R ( B, ADD ); }
static int add_A_C (void) { OPVAR_A_R ( C, ADD ); }
static int add_A_D (void) { OPVAR_A_R ( D, ADD ); }
static int add_A_E (void) { OPVAR_A_R ( E, ADD ); }
static int add_A_H (void) { OPVAR_A_R ( H, ADD ); }
static int add_A_L (void) { OPVAR_A_R ( L, ADD ); }
static int add_A_A (void) { OPVAR_A_A ( ADD ); }
static int add_A_n (void) { OPVAR_A_N ( ADD ); }
static int add_A_pHL (void) { OPVAR_A_pHL ( ADD ); }
static int add_A_pIXd (void) { OPVAR_A_pId ( ADD, IX ); }
static int add_A_pIYd (void) { OPVAR_A_pId ( ADD, IY ); }
static int add_A_IXL (void) { OPVAR_A_IdL ( IX, ADD ); }
static int add_A_IYL (void) { OPVAR_A_IdL ( IY, ADD ); }
static int adc_A_B (void) { OPVAR_A_R ( B, ADC ); }
static int adc_A_C (void) { OPVAR_A_R ( C, ADC ); }
static int adc_A_D (void) { OPVAR_A_R ( D, ADC ); }
static int adc_A_E (void) { OPVAR_A_R ( E, ADC ); }
static int adc_A_H (void) { OPVAR_A_R ( H, ADC ); }
static int adc_A_L (void) { OPVAR_A_R ( L, ADC ); }
static int adc_A_A (void) { OPVAR_A_A ( ADC ); }
static int adc_A_n (void) { OPVAR_A_N ( ADC ); }
static int adc_A_pHL (void) { OPVAR_A_pHL ( ADC ); }
static int adc_A_pIXd (void) { OPVAR_A_pId ( ADC, IX ); }
static int adc_A_pIYd (void) { OPVAR_A_pId ( ADC, IY ); }
static int adc_A_IXH (void) { OPVAR_A_IdH ( IX, ADC ); }
static int adc_A_IYH (void) { OPVAR_A_IdH ( IY, ADC ); }
static int sub_A_B (void) { OPVAR_A_R ( B, SUB ); }
static int sub_A_C (void) { OPVAR_A_R ( C, SUB ); }
static int sub_A_D (void) { OPVAR_A_R ( D, SUB ); }
static int sub_A_E (void) { OPVAR_A_R ( E, SUB ); }
static int sub_A_H (void) { OPVAR_A_R ( H, SUB ); }
static int sub_A_L (void) { OPVAR_A_R ( L, SUB ); }
static int sub_A_A (void) { OPVAR_A_A ( SUB ); }
static int sub_A_n (void) { OPVAR_A_N ( SUB ); }
static int sub_A_pHL (void) { OPVAR_A_pHL ( SUB ); }
static int sub_A_pIXd (void) { OPVAR_A_pId ( SUB, IX ); }
static int sub_A_pIYd (void) { OPVAR_A_pId ( SUB, IY ); }
static int sub_A_IXL (void) { OPVAR_A_IdL ( IX, SUB ); }
static int sub_A_IYL (void) { OPVAR_A_IdL ( IY, SUB ); }
static int sub_A_IXH (void) { OPVAR_A_IdH ( IX, SUB ); }
static int sub_A_IYH (void) { OPVAR_A_IdH ( IY, SUB ); }
static int sbc_A_B (void) { OPVAR_A_R ( B, SBC ); }
static int sbc_A_C (void) { OPVAR_A_R ( C, SBC ); }
static int sbc_A_D (void) { OPVAR_A_R ( D, SBC ); }
static int sbc_A_E (void) { OPVAR_A_R ( E, SBC ); }
static int sbc_A_H (void) { OPVAR_A_R ( H, SBC ); }
static int sbc_A_L (void) { OPVAR_A_R ( L, SBC ); }
static int sbc_A_A (void) { OPVAR_A_A ( SBC ); }
static int sbc_A_n (void) { OPVAR_A_N ( SBC ); }
static int sbc_A_pHL (void) { OPVAR_A_pHL ( SBC ); }
static int sbc_A_pIXd (void) { OPVAR_A_pId ( SBC, IX ); }
static int sbc_A_pIYd (void) { OPVAR_A_pId ( SBC, IY ); }
static int and_A_B (void) { OP_A_R ( B, AND ); }
static int and_A_C (void) { OP_A_R ( C, AND ); }
static int and_A_D (void) { OP_A_R ( D, AND ); }
static int and_A_E (void) { OP_A_R ( E, AND ); }
static int and_A_H (void) { OP_A_R ( H, AND ); }
static int and_A_L (void) { OP_A_R ( L, AND ); }
static int and_A_A (void) { OP_A_R ( A, AND ); }
static int and_A_n (void) { OP_A_N ( AND ); }
static int and_A_pHL (void) { OP_A_pHL ( AND ); }
static int and_A_pIXd (void) { OP_A_pId ( AND, IX ); }
static int and_A_pIYd (void) { OP_A_pId ( AND, IY ); }
static int or_A_B (void) { OP_A_R ( B, OR ); }
static int or_A_C (void) { OP_A_R ( C, OR ); }
static int or_A_D (void) { OP_A_R ( D, OR ); }
static int or_A_E (void) { OP_A_R ( E, OR ); }
static int or_A_H (void) { OP_A_R ( H, OR ); }
static int or_A_L (void) { OP_A_R ( L, OR ); }
static int or_A_A (void) { OP_A_R ( A, OR ); }
static int or_A_n (void) { OP_A_N ( OR ); }
static int or_A_pHL (void) { OP_A_pHL ( OR ); }
static int or_A_pIXd (void) { OP_A_pId ( OR, IX ); }
static int or_A_IXL (void) { OP_A_IL ( OR, IX ); }
static int or_A_IXH (void) { OP_A_IH ( OR, IX ); }
static int or_A_pIYd (void) { OP_A_pId ( OR, IY ); }
static int or_A_IYL (void) { OP_A_IL ( OR, IY ); }
static int or_A_IYH (void) { OP_A_IH ( OR, IY ); }
static int xor_A_B (void) { OP_A_R ( B, XOR ); }
static int xor_A_C (void) { OP_A_R ( C, XOR ); }
static int xor_A_D (void) { OP_A_R ( D, XOR ); }
static int xor_A_E (void) { OP_A_R ( E, XOR ); }
static int xor_A_H (void) { OP_A_R ( H, XOR ); }
static int xor_A_L (void) { OP_A_R ( L, XOR ); }
static int xor_A_A (void) { OP_A_R ( A, XOR ); }
static int xor_A_n (void) { OP_A_N ( XOR ); }
static int xor_A_pHL (void) { OP_A_pHL ( XOR ); }
static int xor_A_pIXd (void) { OP_A_pId ( XOR, IX ); }
static int xor_A_pIYd (void) { OP_A_pId ( XOR, IY ); }
static int cp_A_B (void) { CP_A_R ( B ); }
static int cp_A_C (void) { CP_A_R ( C ); }
static int cp_A_D (void) { CP_A_R ( D ); }
static int cp_A_E (void) { CP_A_R ( E ); }
static int cp_A_H (void) { CP_A_R ( H ); }
static int cp_A_L (void) { CP_A_R ( L ); }
static int cp_A_A (void) { CP_A_R ( A ); }
static int cp_A_n (void) { CP_A_N; }
static int cp_A_pHL (void) { CP_A_pHL; }
static int cp_A_pIXd (void) { CP_A_pId ( IX ); }
static int cp_A_pIYd (void) { CP_A_pId ( IY ); }
static int cp_A_IXL (void) { CP_A_IL ( IX ); }
static int cp_A_IYL (void) { CP_A_IL ( IY ); }
static int cp_A_IXH (void) { CP_A_IH ( IX ); }
static int cp_A_IYH (void) { CP_A_IH ( IY ); }
static int inc_B (void) { INCDEC_R ( B, INC ); }
static int inc_C (void) { INCDEC_R ( C, INC ); }
static int inc_D (void) { INCDEC_R ( D, INC ); }
static int inc_E (void) { INCDEC_R ( E, INC ); }
static int inc_H (void) { INCDEC_R ( H, INC ); }
static int inc_L (void) { INCDEC_R ( L, INC ); }
static int inc_A (void) { INCDEC_A ( INC ); }
static int inc_pHL (void) { INCDEC_pHL ( INC ); }
static int inc_pIXd (void) { INCDEC_pId ( IX, INC ); }
static int inc_pIYd (void) { INCDEC_pId ( IY, INC ); }
static int dec_B (void) { INCDEC_R ( B, DEC ); }
static int dec_C (void) { INCDEC_R ( C, DEC ); }
static int dec_D (void) { INCDEC_R ( D, DEC ); }
static int dec_E (void) { INCDEC_R ( E, DEC ); }
static int dec_H (void) { INCDEC_R ( H, DEC ); }
static int dec_L (void) { INCDEC_R ( L, DEC ); }
static int dec_A (void) { INCDEC_A ( DEC ); }
static int dec_pHL (void) { INCDEC_pHL ( DEC ); }
static int dec_pIXd (void) { INCDEC_pId ( IX, DEC ); }
static int dec_pIYd (void) { INCDEC_pId ( IY, DEC ); }


/* GENERAL-PURPOSE ARITHMETIC AND CPU CONTROL GROUPS */

static int daa (void)
{
  _regs.A= _daa_table[ (((Z80u16) (_regs.F&HFLAG))<<6) |
        	       (((Z80u16) (_regs.F&NFLAG))<<8) |
        	       (((Z80u16) (_regs.F&CFLAG))<<8) |
        	       _regs.A ];
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|CFLAG );
  _regs.F|= _regs.A>>8;
  _regs.A&= 0xff;
  return 4;
}
static int cpl (void)
{
  _regs.A= (Z80u8) (~_regs.A);
  _regs.F|= HFLAG|NFLAG;
  return 4;
}
static int neg (void)
{
  Z80u8 aux;
  aux= (Z80u8) _regs.A;
  _regs.A=  C2(aux);
  RESET_FLAGS ( SFLAG|ZFLAG|HFLAG|PVFLAG|CFLAG );
  _regs.F|=
    (_regs.A?0:ZFLAG) | (aux?CFLAG:0) | ((aux==0x80)?PVFLAG:0) |
    ((~(C1(aux)^_regs.A))&HFLAG) | (_regs.A&SFLAG) | NFLAG;
  return 8;
}
static int ccf (void) { RESET_FLAGS ( NFLAG ); _regs.F^= CFLAG; return 4; }
static int scf (void)
{
  RESET_FLAGS ( HFLAG|NFLAG );
  _regs.F|= CFLAG;
  return 4;
}
static int nop (void) { return 4; }
static int halt (void)
{
  if ( _regs.halted && _regs.unhalted )
    {
      _regs.halted= Z80_FALSE;
      _regs.unhalted= Z80_FALSE;
    }
  else
    {
      if ( !_regs.halted )
        {
          _regs.halted= Z80_TRUE;
          _regs.unhalted= Z80_FALSE;
        }
      --_regs.PC;
    }
  return 4;
}
static int di (void) { _regs.IFF1= _regs.IFF2= Z80_FALSE; return 4; }
static int ei (void) { _regs.enable_IFF= Z80_TRUE; return 4; }
static int im0 (void) { _regs.imode= 0; return 8; }
static int im1 (void) { _regs.imode= 1; return 8; }
static int im2 (void) { _regs.imode= 2; return 8; }


/* 16-BIT ARITHMETIC GROUP */

static int add_HL_BC (void) { OP_HL_SS_NORET ( ADD, B, C ); return 11; }
static int add_HL_DE (void) { OP_HL_SS_NORET ( ADD, D, E ); return 11; }
static int add_HL_HL (void) { OP_HL_SS_NORET ( ADD, H, L ); return 11; }
static int add_HL_SP (void) { OP_HL_RU16_NORET ( ADD, SP ); return 11; }
static int adc_HL_BC (void) { OP_HL_SS_NORET ( ADC, B, C ); return 15; }
static int adc_HL_DE (void) { OP_HL_SS_NORET ( ADC, D, E ); return 15; }
static int adc_HL_HL (void) { OP_HL_SS_NORET ( ADC, H, L ); return 15; }
static int adc_HL_SP (void) { OP_HL_RU16_NORET ( ADC, SP ); return 15; }
static int sbc_HL_BC (void) { OP_HL_SS_NORET ( SBC, B, C ); return 15; }
static int sbc_HL_DE (void) { OP_HL_SS_NORET ( SBC, D, E ); return 15; }
static int sbc_HL_HL (void) { OP_HL_SS_NORET ( SBC, H, L ); return 15; }
static int sbc_HL_SP (void) { OP_HL_RU16_NORET ( SBC, SP ); return 15; }
static int add_IX_BC (void) { ADD_I_PP ( IX, B, C ); }
static int add_IX_DE (void) { ADD_I_PP ( IX, D, E ); }
static int add_IX_IX (void) { ADD_I_RU16 ( IX, IX ); }
static int add_IX_SP (void) { ADD_I_RU16 ( IX, SP ); }
static int add_IY_BC (void) { ADD_I_PP ( IY, B, C ); }
static int add_IY_DE (void) { ADD_I_PP ( IY, D, E ); }
static int add_IY_IY (void) { ADD_I_RU16 ( IY, IY ); }
static int add_IY_SP (void) { ADD_I_RU16 ( IY, SP ); }
static int inc_BC (void) { INCDEC_SS ( INC, B, C ); }
static int inc_DE (void) { INCDEC_SS ( INC, D, E ); }
static int inc_HL (void) { INCDEC_SS ( INC, H, L ); }
static int inc_SP (void) { ++_regs.SP; return 6; }
static int inc_IX (void) { ++_regs.IX; return 10; }
static int inc_IXL (void) { INCDEC_IdL ( IX, INC ); }
static int inc_IXH (void) { INCDEC_IdH ( IX, INC ); }
static int inc_IY (void) { ++_regs.IY; return 10; }
static int inc_IYL (void) { INCDEC_IdL ( IY, INC ); }
static int inc_IYH (void) { INCDEC_IdH ( IY, INC ); }
static int dec_BC (void) { INCDEC_SS ( DEC, B, C ); }
static int dec_DE (void) { INCDEC_SS ( DEC, D, E ); }
static int dec_HL (void) { INCDEC_SS ( DEC, H, L ); }
static int dec_SP (void) { --_regs.SP; return 6; }
static int dec_IX (void) { --_regs.IX; return 10; }
static int dec_IXL (void) { INCDEC_IdL ( IX, DEC ); }
static int dec_IXH (void) { INCDEC_IdH ( IX, DEC ); }
static int dec_IY (void) { --_regs.IY; return 10; }
static int dec_IYL (void) { INCDEC_IdL ( IY, DEC ); }
static int dec_IYH (void) { INCDEC_IdH ( IY, DEC ); }


/* ROTATE AND SHIFT GROUP */

static int rlca (void)
{
  Z80u8 aux;
  RESET_FLAGS ( HFLAG|NFLAG|CFLAG );
  aux= _regs.A>>7;
  _regs.F|= aux;
  _regs.A= ((_regs.A<<1)|aux)&0xff;
  return 4;
}
static int rla (void)
{
  _regs.A<<= 1;
  _regs.A|= _regs.F&CFLAG;
  RESET_FLAGS ( HFLAG|NFLAG|CFLAG );
  _regs.F|= _regs.A>>8;
  _regs.A&= 0xff;
  return 4;
}
static int rrca (void)
{
  Z80u8 aux;
  RESET_FLAGS ( HFLAG|NFLAG|CFLAG );
  aux= _regs.A&0x1;
  _regs.F|= aux;
  _regs.A= (_regs.A>>1)|(aux<<7);
  return 4;
}
static int rra (void)
{
  Z80u8 aux;
  aux= _regs.A&0x1;
  _regs.A= (_regs.A>>1)|((_regs.F&CFLAG)<<7);
  RESET_FLAGS ( HFLAG|NFLAG|CFLAG );
  _regs.F|= aux;
  return 4;
}
static int rlc_B (void) { ROT_R ( RLC, B ); }
static int rlc_C (void) { ROT_R ( RLC, C ); }
static int rlc_D (void) { ROT_R ( RLC, D ); }
static int rlc_E (void) { ROT_R ( RLC, E ); }
static int rlc_H (void) { ROT_R ( RLC, H ); }
static int rlc_L (void) { ROT_R ( RLC, L ); }
static int rlc_A (void) { ROT_A ( RLC ); }
static int rlc_pHL (void) { ROT_pHL ( RLC ); }
static int rlc_pIXd ( const Z80s8 desp ) { ROT_pId ( RLC, IX ); }
static int rlc_pIYd ( const Z80s8 desp ) { ROT_pId ( RLC, IY ); }
static int rl_B (void) { ROT_R ( RL, B ); }
static int rl_C (void) { ROT_R ( RL, C ); }
static int rl_D (void) { ROT_R ( RL, D ); }
static int rl_E (void) { ROT_R ( RL, E ); }
static int rl_H (void) { ROT_R ( RL, H ); }
static int rl_L (void) { ROT_R ( RL, L ); }
static int rl_A (void) { ROT_A ( RL ); }
static int rl_pHL (void) { ROT_pHL ( RL ); }
static int rl_pIXd ( const Z80s8 desp ) { ROT_pId ( RL, IX ); }
static int rl_pIYd ( const Z80s8 desp ) { ROT_pId ( RL, IY ); }
static int rrc_B (void) { ROT_R ( RRC, B ); }
static int rrc_C (void) { ROT_R ( RRC, C ); }
static int rrc_D (void) { ROT_R ( RRC, D ); }
static int rrc_E (void) { ROT_R ( RRC, E ); }
static int rrc_H (void) { ROT_R ( RRC, H ); }
static int rrc_L (void) { ROT_R ( RRC, L ); }
static int rrc_A (void) { ROT_A ( RRC ); }
static int rrc_pHL (void) { ROT_pHL ( RRC ); }
static int rrc_pIXd ( const Z80s8 desp ) { ROT_pId ( RRC, IX ); }
static int rrc_pIYd ( const Z80s8 desp ) { ROT_pId ( RRC, IY ); }
static int rr_B (void) { ROT_R ( RR, B ); }
static int rr_C (void) { ROT_R ( RR, C ); }
static int rr_D (void) { ROT_R ( RR, D ); }
static int rr_E (void) { ROT_R ( RR, E ); }
static int rr_H (void) { ROT_R ( RR, H ); }
static int rr_L (void) { ROT_R ( RR, L ); }
static int rr_A (void) { ROT_A ( RR ); }
static int rr_pHL (void) { ROT_pHL ( RR ); }
static int rr_pIXd ( const Z80s8 desp ) { ROT_pId ( RR, IX ); }
static int rr_pIYd ( const Z80s8 desp ) { ROT_pId ( RR, IY ); }
static int sla_B (void) { SHI_R ( SLA, B ); }
static int sla_C (void) { SHI_R ( SLA, C ); }
static int sla_D (void) { SHI_R ( SLA, D ); }
static int sla_E (void) { SHI_R ( SLA, E ); }
static int sla_H (void) { SHI_R ( SLA, H ); }
static int sla_L (void) { SHI_R ( SLA, L ); }
static int sla_A (void) { SHI_A ( SLA ); }
static int sla_pHL (void) { SHI_pHL ( SLA ); }
static int sla_pIXd ( const Z80s8 desp ) { SHI_pId ( SLA, IX ); }
static int sla_pIYd ( const Z80s8 desp ) { SHI_pId ( SLA, IY ); }
static int sra_B (void) { SHI_R ( SRA, B ); }
static int sra_C (void) { SHI_R ( SRA, C ); }
static int sra_D (void) { SHI_R ( SRA, D ); }
static int sra_E (void) { SHI_R ( SRA, E ); }
static int sra_H (void) { SHI_R ( SRA, H ); }
static int sra_L (void) { SHI_R ( SRA, L ); }
static int sra_A (void) { SHI_A ( SRA ); }
static int sra_pHL (void) { SHI_pHL ( SRA ); }
static int sra_pIXd ( const Z80s8 desp ) { SHI_pId ( SRA, IX ); }
static int sra_pIYd ( const Z80s8 desp ) { SHI_pId ( SRA, IY ); }
static int srl_B (void) { SHI_R ( SRL, B ); }
static int srl_C (void) { SHI_R ( SRL, C ); }
static int srl_D (void) { SHI_R ( SRL, D ); }
static int srl_E (void) { SHI_R ( SRL, E ); }
static int srl_H (void) { SHI_R ( SRL, H ); }
static int srl_L (void) { SHI_R ( SRL, L ); }
static int srl_A (void) { SHI_A ( SRL ); }
static int srl_pHL (void) { SHI_pHL ( SRL ); }
static int srl_pIXd ( const Z80s8 desp ) { SHI_pId ( SRL, IX ); }
static int srl_pIYd ( const Z80s8 desp ) { SHI_pId ( SRL, IY ); }
static int rld (void)
{
  Z80u16 addr;
  Z80u8 var;
  addr= R16 ( H, L );
  var= Z80_read ( addr );
  Z80_write ( addr, (var<<4)|(_regs.A&0xf) );
  _regs.A= (_regs.A&0xf0)|(var>>4);
  RLRD_SET_FLAGS;
  return 18;
}
static int rrd (void)
{
  Z80u16 addr;
  Z80u8 var;
  addr= R16 ( H, L );
  var= Z80_read ( addr );
  Z80_write ( addr, (var>>4)|((_regs.A&0xf)<<4) );
  _regs.A= (_regs.A&0xf0)|(var&0xf);
  RLRD_SET_FLAGS;
  return 18;
}


/* BIT SET, RESET, AND TEST GROUP */

static int bit_0_B (void) { BIT_R ( B, 0x01 ); }
static int bit_0_C (void) { BIT_R ( C, 0x01 ); }
static int bit_0_D (void) { BIT_R ( D, 0x01 ); }
static int bit_0_E (void) { BIT_R ( E, 0x01 ); }
static int bit_0_H (void) { BIT_R ( H, 0x01 ); }
static int bit_0_L (void) { BIT_R ( L, 0x01 ); }
static int bit_0_A (void) { BIT_R ( A, 0x01 ); }
static int bit_1_B (void) { BIT_R ( B, 0x02 ); }
static int bit_1_C (void) { BIT_R ( C, 0x02 ); }
static int bit_1_D (void) { BIT_R ( D, 0x02 ); }
static int bit_1_E (void) { BIT_R ( E, 0x02 ); }
static int bit_1_H (void) { BIT_R ( H, 0x02 ); }
static int bit_1_L (void) { BIT_R ( L, 0x02 ); }
static int bit_1_A (void) { BIT_R ( A, 0x02 ); }
static int bit_2_B (void) { BIT_R ( B, 0x04 ); }
static int bit_2_C (void) { BIT_R ( C, 0x04 ); }
static int bit_2_D (void) { BIT_R ( D, 0x04 ); }
static int bit_2_E (void) { BIT_R ( E, 0x04 ); }
static int bit_2_H (void) { BIT_R ( H, 0x04 ); }
static int bit_2_L (void) { BIT_R ( L, 0x04 ); }
static int bit_2_A (void) { BIT_R ( A, 0x04 ); }
static int bit_3_B (void) { BIT_R ( B, 0x08 ); }
static int bit_3_C (void) { BIT_R ( C, 0x08 ); }
static int bit_3_D (void) { BIT_R ( D, 0x08 ); }
static int bit_3_E (void) { BIT_R ( E, 0x08 ); }
static int bit_3_H (void) { BIT_R ( H, 0x08 ); }
static int bit_3_L (void) { BIT_R ( L, 0x08 ); }
static int bit_3_A (void) { BIT_R ( A, 0x08 ); }
static int bit_4_B (void) { BIT_R ( B, 0x10 ); }
static int bit_4_C (void) { BIT_R ( C, 0x10 ); }
static int bit_4_D (void) { BIT_R ( D, 0x10 ); }
static int bit_4_E (void) { BIT_R ( E, 0x10 ); }
static int bit_4_H (void) { BIT_R ( H, 0x10 ); }
static int bit_4_L (void) { BIT_R ( L, 0x10 ); }
static int bit_4_A (void) { BIT_R ( A, 0x10 ); }
static int bit_5_B (void) { BIT_R ( B, 0x20 ); }
static int bit_5_C (void) { BIT_R ( C, 0x20 ); }
static int bit_5_D (void) { BIT_R ( D, 0x20 ); }
static int bit_5_E (void) { BIT_R ( E, 0x20 ); }
static int bit_5_H (void) { BIT_R ( H, 0x20 ); }
static int bit_5_L (void) { BIT_R ( L, 0x20 ); }
static int bit_5_A (void) { BIT_R ( A, 0x20 ); }
static int bit_6_B (void) { BIT_R ( B, 0x40 ); }
static int bit_6_C (void) { BIT_R ( C, 0x40 ); }
static int bit_6_D (void) { BIT_R ( D, 0x40 ); }
static int bit_6_E (void) { BIT_R ( E, 0x40 ); }
static int bit_6_H (void) { BIT_R ( H, 0x40 ); }
static int bit_6_L (void) { BIT_R ( L, 0x40 ); }
static int bit_6_A (void) { BIT_R ( A, 0x40 ); }
static int bit_7_B (void) { BIT_R ( B, 0x80 ); }
static int bit_7_C (void) { BIT_R ( C, 0x80 ); }
static int bit_7_D (void) { BIT_R ( D, 0x80 ); }
static int bit_7_E (void) { BIT_R ( E, 0x80 ); }
static int bit_7_H (void) { BIT_R ( H, 0x80 ); }
static int bit_7_L (void) { BIT_R ( L, 0x80 ); }
static int bit_7_A (void) { BIT_R ( A, 0x80 ); }
static int bit_0_pHL (void) { BIT_pHL ( 0x01 ); }
static int bit_1_pHL (void) { BIT_pHL ( 0x02 ); }
static int bit_2_pHL (void) { BIT_pHL ( 0x04 ); }
static int bit_3_pHL (void) { BIT_pHL ( 0x08 ); }
static int bit_4_pHL (void) { BIT_pHL ( 0x10 ); }
static int bit_5_pHL (void) { BIT_pHL ( 0x20 ); }
static int bit_6_pHL (void) { BIT_pHL ( 0x40 ); }
static int bit_7_pHL (void) { BIT_pHL ( 0x80 ); }
static int bit_0_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x01 ); }
static int bit_1_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x02 ); }
static int bit_2_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x04 ); }
static int bit_3_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x08 ); }
static int bit_4_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x10 ); }
static int bit_5_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x20 ); }
static int bit_6_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x40 ); }
static int bit_7_pIXd ( const Z80s8 desp ) { BIT_pId ( IX, 0x80 ); }
static int bit_0_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x01 ); }
static int bit_1_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x02 ); }
static int bit_2_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x04 ); }
static int bit_3_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x08 ); }
static int bit_4_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x10 ); }
static int bit_5_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x20 ); }
static int bit_6_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x40 ); }
static int bit_7_pIYd ( const Z80s8 desp ) { BIT_pId ( IY, 0x80 ); }
static int set_0_B (void) { SET_R ( B, 0x01 ); }
static int set_0_C (void) { SET_R ( C, 0x01 ); }
static int set_0_D (void) { SET_R ( D, 0x01 ); }
static int set_0_E (void) { SET_R ( E, 0x01 ); }
static int set_0_H (void) { SET_R ( H, 0x01 ); }
static int set_0_L (void) { SET_R ( L, 0x01 ); }
static int set_0_A (void) { SET_R ( A, 0x01 ); }
static int set_1_B (void) { SET_R ( B, 0x02 ); }
static int set_1_C (void) { SET_R ( C, 0x02 ); }
static int set_1_D (void) { SET_R ( D, 0x02 ); }
static int set_1_E (void) { SET_R ( E, 0x02 ); }
static int set_1_H (void) { SET_R ( H, 0x02 ); }
static int set_1_L (void) { SET_R ( L, 0x02 ); }
static int set_1_A (void) { SET_R ( A, 0x02 ); }
static int set_2_B (void) { SET_R ( B, 0x04 ); }
static int set_2_C (void) { SET_R ( C, 0x04 ); }
static int set_2_D (void) { SET_R ( D, 0x04 ); }
static int set_2_E (void) { SET_R ( E, 0x04 ); }
static int set_2_H (void) { SET_R ( H, 0x04 ); }
static int set_2_L (void) { SET_R ( L, 0x04 ); }
static int set_2_A (void) { SET_R ( A, 0x04 ); }
static int set_3_B (void) { SET_R ( B, 0x08 ); }
static int set_3_C (void) { SET_R ( C, 0x08 ); }
static int set_3_D (void) { SET_R ( D, 0x08 ); }
static int set_3_E (void) { SET_R ( E, 0x08 ); }
static int set_3_H (void) { SET_R ( H, 0x08 ); }
static int set_3_L (void) { SET_R ( L, 0x08 ); }
static int set_3_A (void) { SET_R ( A, 0x08 ); }
static int set_4_B (void) { SET_R ( B, 0x10 ); }
static int set_4_C (void) { SET_R ( C, 0x10 ); }
static int set_4_D (void) { SET_R ( D, 0x10 ); }
static int set_4_E (void) { SET_R ( E, 0x10 ); }
static int set_4_H (void) { SET_R ( H, 0x10 ); }
static int set_4_L (void) { SET_R ( L, 0x10 ); }
static int set_4_A (void) { SET_R ( A, 0x10 ); }
static int set_5_B (void) { SET_R ( B, 0x20 ); }
static int set_5_C (void) { SET_R ( C, 0x20 ); }
static int set_5_D (void) { SET_R ( D, 0x20 ); }
static int set_5_E (void) { SET_R ( E, 0x20 ); }
static int set_5_H (void) { SET_R ( H, 0x20 ); }
static int set_5_L (void) { SET_R ( L, 0x20 ); }
static int set_5_A (void) { SET_R ( A, 0x20 ); }
static int set_6_B (void) { SET_R ( B, 0x40 ); }
static int set_6_C (void) { SET_R ( C, 0x40 ); }
static int set_6_D (void) { SET_R ( D, 0x40 ); }
static int set_6_E (void) { SET_R ( E, 0x40 ); }
static int set_6_H (void) { SET_R ( H, 0x40 ); }
static int set_6_L (void) { SET_R ( L, 0x40 ); }
static int set_6_A (void) { SET_R ( A, 0x40 ); }
static int set_7_B (void) { SET_R ( B, 0x80 ); }
static int set_7_C (void) { SET_R ( C, 0x80 ); }
static int set_7_D (void) { SET_R ( D, 0x80 ); }
static int set_7_E (void) { SET_R ( E, 0x80 ); }
static int set_7_H (void) { SET_R ( H, 0x80 ); }
static int set_7_L (void) { SET_R ( L, 0x80 ); }
static int set_7_A (void) { SET_R ( A, 0x80 ); }
static int set_0_pHL (void) { SET_pHL ( 0x01 ); }
static int set_1_pHL (void) { SET_pHL ( 0x02 ); }
static int set_2_pHL (void) { SET_pHL ( 0x04 ); }
static int set_3_pHL (void) { SET_pHL ( 0x08 ); }
static int set_4_pHL (void) { SET_pHL ( 0x10 ); }
static int set_5_pHL (void) { SET_pHL ( 0x20 ); }
static int set_6_pHL (void) { SET_pHL ( 0x40 ); }
static int set_7_pHL (void) { SET_pHL ( 0x80 ); }
static int set_0_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x01 ); }
static int set_1_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x02 ); }
static int set_2_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x04 ); }
static int set_3_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x08 ); }
static int set_4_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x10 ); }
static int set_5_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x20 ); }
static int set_6_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x40 ); }
static int set_7_pIXd ( const Z80s8 desp ) { SET_pId ( IX, 0x80 ); }
static int set_0_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x01 ); }
static int set_1_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x02 ); }
static int set_2_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x04 ); }
static int set_3_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x08 ); }
static int set_4_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x10 ); }
static int set_5_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x20 ); }
static int set_6_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x40 ); }
static int set_7_pIYd ( const Z80s8 desp ) { SET_pId ( IY, 0x80 ); }
static int res_0_B (void) { RES_R ( B, 0xfe ); }
static int res_0_C (void) { RES_R ( C, 0xfe ); }
static int res_0_D (void) { RES_R ( D, 0xfe ); }
static int res_0_E (void) { RES_R ( E, 0xfe ); }
static int res_0_H (void) { RES_R ( H, 0xfe ); }
static int res_0_L (void) { RES_R ( L, 0xfe ); }
static int res_0_A (void) { RES_R ( A, 0xfe ); }
static int res_1_B (void) { RES_R ( B, 0xfd ); }
static int res_1_C (void) { RES_R ( C, 0xfd ); }
static int res_1_D (void) { RES_R ( D, 0xfd ); }
static int res_1_E (void) { RES_R ( E, 0xfd ); }
static int res_1_H (void) { RES_R ( H, 0xfd ); }
static int res_1_L (void) { RES_R ( L, 0xfd ); }
static int res_1_A (void) { RES_R ( A, 0xfd ); }
static int res_2_B (void) { RES_R ( B, 0xfb ); }
static int res_2_C (void) { RES_R ( C, 0xfb ); }
static int res_2_D (void) { RES_R ( D, 0xfb ); }
static int res_2_E (void) { RES_R ( E, 0xfb ); }
static int res_2_H (void) { RES_R ( H, 0xfb ); }
static int res_2_L (void) { RES_R ( L, 0xfb ); }
static int res_2_A (void) { RES_R ( A, 0xfb ); }
static int res_3_B (void) { RES_R ( B, 0xf7 ); }
static int res_3_C (void) { RES_R ( C, 0xf7 ); }
static int res_3_D (void) { RES_R ( D, 0xf7 ); }
static int res_3_E (void) { RES_R ( E, 0xf7 ); }
static int res_3_H (void) { RES_R ( H, 0xf7 ); }
static int res_3_L (void) { RES_R ( L, 0xf7 ); }
static int res_3_A (void) { RES_R ( A, 0xf7 ); }
static int res_4_B (void) { RES_R ( B, 0xef ); }
static int res_4_C (void) { RES_R ( C, 0xef ); }
static int res_4_D (void) { RES_R ( D, 0xef ); }
static int res_4_E (void) { RES_R ( E, 0xef ); }
static int res_4_H (void) { RES_R ( H, 0xef ); }
static int res_4_L (void) { RES_R ( L, 0xef ); }
static int res_4_A (void) { RES_R ( A, 0xef ); }
static int res_5_B (void) { RES_R ( B, 0xdf ); }
static int res_5_C (void) { RES_R ( C, 0xdf ); }
static int res_5_D (void) { RES_R ( D, 0xdf ); }
static int res_5_E (void) { RES_R ( E, 0xdf ); }
static int res_5_H (void) { RES_R ( H, 0xdf ); }
static int res_5_L (void) { RES_R ( L, 0xdf ); }
static int res_5_A (void) { RES_R ( A, 0xdf ); }
static int res_6_B (void) { RES_R ( B, 0xbf ); }
static int res_6_C (void) { RES_R ( C, 0xbf ); }
static int res_6_D (void) { RES_R ( D, 0xbf ); }
static int res_6_E (void) { RES_R ( E, 0xbf ); }
static int res_6_H (void) { RES_R ( H, 0xbf ); }
static int res_6_L (void) { RES_R ( L, 0xbf ); }
static int res_6_A (void) { RES_R ( A, 0xbf ); }
static int res_7_B (void) { RES_R ( B, 0x7f ); }
static int res_7_C (void) { RES_R ( C, 0x7f ); }
static int res_7_D (void) { RES_R ( D, 0x7f ); }
static int res_7_E (void) { RES_R ( E, 0x7f ); }
static int res_7_H (void) { RES_R ( H, 0x7f ); }
static int res_7_L (void) { RES_R ( L, 0x7f ); }
static int res_7_A (void) { RES_R ( A, 0x7f ); }
static int res_0_pHL (void) { RES_pHL ( 0xfe ); }
static int res_1_pHL (void) { RES_pHL ( 0xfd ); }
static int res_2_pHL (void) { RES_pHL ( 0xfb ); }
static int res_3_pHL (void) { RES_pHL ( 0xf7 ); }
static int res_4_pHL (void) { RES_pHL ( 0xef ); }
static int res_5_pHL (void) { RES_pHL ( 0xdf ); }
static int res_6_pHL (void) { RES_pHL ( 0xbf ); }
static int res_7_pHL (void) { RES_pHL ( 0x7f ); }
static int res_0_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0xfe ); }
static int res_1_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0xfd ); }
static int res_2_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0xfb ); }
static int res_3_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0xf7 ); }
static int res_4_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0xef ); }
static int res_5_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0xdf ); }
static int res_6_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0xbf ); }
static int res_7_pIXd ( const Z80s8 desp ) { RES_pId ( IX, 0x7f ); }
static int res_0_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0xfe ); }
static int res_1_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0xfd ); }
static int res_2_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0xfb ); }
static int res_3_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0xf7 ); }
static int res_4_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0xef ); }
static int res_5_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0xdf ); }
static int res_6_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0xbf ); }
static int res_7_pIYd ( const Z80s8 desp ) { RES_pId ( IY, 0x7f ); }


/* JUMP GROUP */

static int jp (void) { JP (); }
static int jp_NZ (void) { JP_COND ( !(_regs.F&ZFLAG) ); }
static int jp_Z (void) { JP_COND ( _regs.F&ZFLAG ); }
static int jp_NC (void) { JP_COND ( !(_regs.F&CFLAG) ); }
static int jp_C (void) { JP_COND ( _regs.F&CFLAG ); }
static int jp_PO (void) { JP_COND ( !(_regs.F&PVFLAG) ); }
static int jp_PE (void) { JP_COND ( _regs.F&PVFLAG ); }
static int jp_P (void) { JP_COND ( !(_regs.F&SFLAG) ); }
static int jp_M (void) { JP_COND ( _regs.F&SFLAG ); }
static int jr (void) { BRANCH; return 12; }
static int jr_C (void) { JR_COND ( _regs.F&CFLAG ) }
static int jr_NC (void) { JR_COND ( !(_regs.F&CFLAG) ) }
static int jr_Z (void) { JR_COND ( _regs.F&ZFLAG ) }
static int jr_NZ (void) { JR_COND ( !(_regs.F&ZFLAG) ) }
static int jp_HL (void) { _regs.PC= R16 ( H, L ); return 4; }
static int jp_IX (void) { _regs.PC= _regs.IX; return 8; }
static int jp_IY (void) { _regs.PC= _regs.IY; return 8; }
static int djnz (void)
{
  if ( --_regs.B == 0 ) { ++_regs.PC; return 8; }
  else { BRANCH; return 13; }
}


/* CALL AND RETURN GROUP */

static int call (void) { Z80u16 aux; CALL_NORET ( aux ); return 17; }
static int call_NZ (void) { CALL_COND ( !(_regs.F&ZFLAG) ); }
static int call_Z (void) { CALL_COND ( _regs.F&ZFLAG ); }
static int call_NC (void) { CALL_COND ( !(_regs.F&CFLAG) ); }
static int call_C (void) { CALL_COND ( _regs.F&CFLAG ); }
static int call_PO (void) { CALL_COND ( !(_regs.F&PVFLAG) ); }
static int call_PE (void) { CALL_COND ( _regs.F&PVFLAG ); }
static int call_P (void) { CALL_COND ( !(_regs.F&SFLAG) ); }
static int call_M (void) { CALL_COND ( _regs.F&SFLAG ); }
static int ret (void) { RET_NORET; return 10; }
static int ret_NZ (void) { RET_COND ( !(_regs.F&ZFLAG) ); }
static int ret_Z (void) { RET_COND ( _regs.F&ZFLAG ); }
static int ret_NC (void) { RET_COND ( !(_regs.F&CFLAG) ); }
static int ret_C (void) { RET_COND ( _regs.F&CFLAG ); }
static int ret_PO (void) { RET_COND ( !(_regs.F&PVFLAG) ); }
static int ret_PE (void) { RET_COND ( _regs.F&PVFLAG ); }
static int ret_P (void) { RET_COND ( !(_regs.F&SFLAG) ); }
static int ret_M (void) { RET_COND ( _regs.F&SFLAG ); }
static int reti (void) { RET_NORET; Z80_reti_signal (); return 14; }
static int retn (void) { RET_NORET; _regs.IFF1= _regs.IFF2; return 14; }
static int rst_00 (void) { RST_P ( 0x00 ); }
static int rst_08 (void) { RST_P ( 0x08 ); }
static int rst_10 (void) { RST_P ( 0x10 ); }
static int rst_18 (void) { RST_P ( 0x18 ); }
static int rst_20 (void) { RST_P ( 0x20 ); }
static int rst_28 (void) { RST_P ( 0x28 ); }
static int rst_30 (void) { RST_P ( 0x30 ); }
static int rst_38 (void) { RST_P ( 0x38 ); }


/* INPUT AND OUTPUT GROUP */

static int in_A_n (void)
{
  _regs.A= Z80_io_read ( Z80_read ( _regs.PC++ ) );
  return 11;
}
static int in_B_C (void) { IN_R_C ( B ); }
static int in_C_C (void) { IN_R_C ( C ); }
static int in_D_C (void) { IN_R_C ( D ); }
static int in_E_C (void) { IN_R_C ( E ); }
static int in_H_C (void) { IN_R_C ( H ); }
static int in_L_C (void) { IN_R_C ( L ); }
static int in_A_C (void) { IN_R_C ( A ); }
static int in_UNK_C (void)
{
  Z80u8 val;
  val= Z80_io_read ( _regs.C );
  IN_C_SETFLAGS ( val );
  return 12;
}
static int ini (void) { INID ( INC ); }
static int inir (void) { INIDR ( INC ); }
static int ind (void) { INID ( DEC ); }
static int indr (void) { INIDR ( DEC ); }
static int out_n_A (void)
{
  Z80_io_write ( Z80_read ( _regs.PC++ ), (Z80u8) _regs.A );
  return 11;
}
static int out_C_B (void) { OUT_C_R ( B ); }
static int out_C_C (void) { OUT_C_R ( C ); }
static int out_C_D (void) { OUT_C_R ( D ); }
static int out_C_E (void) { OUT_C_R ( E ); }
static int out_C_H (void) { OUT_C_R ( H ); }
static int out_C_L (void) { OUT_C_R ( L ); }
static int out_C_A (void) { OUT_C_R ( A ); }
static int outi (void) { OUTID ( INC ); }
static int otir (void) { OUTIDR ( INC ); }
static int outd (void) { OUTID ( DEC ); }
static int otdr (void) { OUTIDR ( DEC ); }


static int (*const _insts_cb[256]) (void)=
{
  /* 0x0 */ rlc_B,
  /* 0x1 */ rlc_C,
  /* 0x2 */ rlc_D,
  /* 0x3 */ rlc_E,
  /* 0x4 */ rlc_H,
  /* 0x5 */ rlc_L,
  /* 0x6 */ rlc_pHL,
  /* 0x7 */ rlc_A,
  /* 0x8 */ rrc_B,
  /* 0x9 */ rrc_C,
  /* 0xa */ rrc_D,
  /* 0xb */ rrc_E,
  /* 0xc */ rrc_H,
  /* 0xd */ rrc_L,
  /* 0xe */ rrc_pHL,
  /* 0xf */ rrc_A,
  /* 0x10 */ rl_B,
  /* 0x11 */ rl_C,
  /* 0x12 */ rl_D,
  /* 0x13 */ rl_E,
  /* 0x14 */ rl_H,
  /* 0x15 */ rl_L,
  /* 0x16 */ rl_pHL,
  /* 0x17 */ rl_A,
  /* 0x18 */ rr_B,
  /* 0x19 */ rr_C,
  /* 0x1a */ rr_D,
  /* 0x1b */ rr_E,
  /* 0x1c */ rr_H,
  /* 0x1d */ rr_L,
  /* 0x1e */ rr_pHL,
  /* 0x1f */ rr_A,
  /* 0x20 */ sla_B,
  /* 0x21 */ sla_C,
  /* 0x22 */ sla_D,
  /* 0x23 */ sla_E,
  /* 0x24 */ sla_H,
  /* 0x25 */ sla_L,
  /* 0x26 */ sla_pHL,
  /* 0x27 */ sla_A,
  /* 0x28 */ sra_B,
  /* 0x29 */ sra_C,
  /* 0x2a */ sra_D,
  /* 0x2b */ sra_E,
  /* 0x2c */ sra_H,
  /* 0x2d */ sra_L,
  /* 0x2e */ sra_pHL,
  /* 0x2f */ sra_A,
  /* 0x30 */ unk2,
  /* 0x31 */ unk2,
  /* 0x32 */ unk2,
  /* 0x33 */ unk2,
  /* 0x34 */ unk2,
  /* 0x35 */ unk2,
  /* 0x36 */ unk2,
  /* 0x37 */ unk2,
  /* 0x38 */ srl_B,
  /* 0x39 */ srl_C,
  /* 0x3a */ srl_D,
  /* 0x3b */ srl_E,
  /* 0x3c */ srl_H,
  /* 0x3d */ srl_L,
  /* 0x3e */ srl_pHL,
  /* 0x3f */ srl_A,
  /* 0x40 */ bit_0_B,
  /* 0x41 */ bit_0_C,
  /* 0x42 */ bit_0_D,
  /* 0x43 */ bit_0_E,
  /* 0x44 */ bit_0_H,
  /* 0x45 */ bit_0_L,
  /* 0x46 */ bit_0_pHL,
  /* 0x47 */ bit_0_A,
  /* 0x48 */ bit_1_B,
  /* 0x49 */ bit_1_C,
  /* 0x4a */ bit_1_D,
  /* 0x4b */ bit_1_E,
  /* 0x4c */ bit_1_H,
  /* 0x4d */ bit_1_L,
  /* 0x4e */ bit_1_pHL,
  /* 0x4f */ bit_1_A,
  /* 0x50 */ bit_2_B,
  /* 0x51 */ bit_2_C,
  /* 0x52 */ bit_2_D,
  /* 0x53 */ bit_2_E,
  /* 0x54 */ bit_2_H,
  /* 0x55 */ bit_2_L,
  /* 0x56 */ bit_2_pHL,
  /* 0x57 */ bit_2_A,
  /* 0x58 */ bit_3_B,
  /* 0x59 */ bit_3_C,
  /* 0x5a */ bit_3_D,
  /* 0x5b */ bit_3_E,
  /* 0x5c */ bit_3_H,
  /* 0x5d */ bit_3_L,
  /* 0x5e */ bit_3_pHL,
  /* 0x5f */ bit_3_A,
  /* 0x60 */ bit_4_B,
  /* 0x61 */ bit_4_C,
  /* 0x62 */ bit_4_D,
  /* 0x63 */ bit_4_E,
  /* 0x64 */ bit_4_H,
  /* 0x65 */ bit_4_L,
  /* 0x66 */ bit_4_pHL,
  /* 0x67 */ bit_4_A,
  /* 0x68 */ bit_5_B,
  /* 0x69 */ bit_5_C,
  /* 0x6a */ bit_5_D,
  /* 0x6b */ bit_5_E,
  /* 0x6c */ bit_5_H,
  /* 0x6d */ bit_5_L,
  /* 0x6e */ bit_5_pHL,
  /* 0x6f */ bit_5_A,
  /* 0x70 */ bit_6_B,
  /* 0x71 */ bit_6_C,
  /* 0x72 */ bit_6_D,
  /* 0x73 */ bit_6_E,
  /* 0x74 */ bit_6_H,
  /* 0x75 */ bit_6_L,
  /* 0x76 */ bit_6_pHL,
  /* 0x77 */ bit_6_A,
  /* 0x78 */ bit_7_B,
  /* 0x79 */ bit_7_C,
  /* 0x7a */ bit_7_D,
  /* 0x7b */ bit_7_E,
  /* 0x7c */ bit_7_H,
  /* 0x7d */ bit_7_L,
  /* 0x7e */ bit_7_pHL,
  /* 0x7f */ bit_7_A,
  /* 0x80 */ res_0_B,
  /* 0x81 */ res_0_C,
  /* 0x82 */ res_0_D,
  /* 0x83 */ res_0_E,
  /* 0x84 */ res_0_H,
  /* 0x85 */ res_0_L,
  /* 0x86 */ res_0_pHL,
  /* 0x87 */ res_0_A,
  /* 0x88 */ res_1_B,
  /* 0x89 */ res_1_C,
  /* 0x8a */ res_1_D,
  /* 0x8b */ res_1_E,
  /* 0x8c */ res_1_H,
  /* 0x8d */ res_1_L,
  /* 0x8e */ res_1_pHL,
  /* 0x8f */ res_1_A,
  /* 0x90 */ res_2_B,
  /* 0x91 */ res_2_C,
  /* 0x92 */ res_2_D,
  /* 0x93 */ res_2_E,
  /* 0x94 */ res_2_H,
  /* 0x95 */ res_2_L,
  /* 0x96 */ res_2_pHL,
  /* 0x97 */ res_2_A,
  /* 0x98 */ res_3_B,
  /* 0x99 */ res_3_C,
  /* 0x9a */ res_3_D,
  /* 0x9b */ res_3_E,
  /* 0x9c */ res_3_H,
  /* 0x9d */ res_3_L,
  /* 0x9e */ res_3_pHL,
  /* 0x9f */ res_3_A,
  /* 0xa0 */ res_4_B,
  /* 0xa1 */ res_4_C,
  /* 0xa2 */ res_4_D,
  /* 0xa3 */ res_4_E,
  /* 0xa4 */ res_4_H,
  /* 0xa5 */ res_4_L,
  /* 0xa6 */ res_4_pHL,
  /* 0xa7 */ res_4_A,
  /* 0xa8 */ res_5_B,
  /* 0xa9 */ res_5_C,
  /* 0xaa */ res_5_D,
  /* 0xab */ res_5_E,
  /* 0xac */ res_5_H,
  /* 0xad */ res_5_L,
  /* 0xae */ res_5_pHL,
  /* 0xaf */ res_5_A,
  /* 0xb0 */ res_6_B,
  /* 0xb1 */ res_6_C,
  /* 0xb2 */ res_6_D,
  /* 0xb3 */ res_6_E,
  /* 0xb4 */ res_6_H,
  /* 0xb5 */ res_6_L,
  /* 0xb6 */ res_6_pHL,
  /* 0xb7 */ res_6_A,
  /* 0xb8 */ res_7_B,
  /* 0xb9 */ res_7_C,
  /* 0xba */ res_7_D,
  /* 0xbb */ res_7_E,
  /* 0xbc */ res_7_H,
  /* 0xbd */ res_7_L,
  /* 0xbe */ res_7_pHL,
  /* 0xbf */ res_7_A,
  /* 0xc0 */ set_0_B,
  /* 0xc1 */ set_0_C,
  /* 0xc2 */ set_0_D,
  /* 0xc3 */ set_0_E,
  /* 0xc4 */ set_0_H,
  /* 0xc5 */ set_0_L,
  /* 0xc6 */ set_0_pHL,
  /* 0xc7 */ set_0_A,
  /* 0xc8 */ set_1_B,
  /* 0xc9 */ set_1_C,
  /* 0xca */ set_1_D,
  /* 0xcb */ set_1_E,
  /* 0xcc */ set_1_H,
  /* 0xcd */ set_1_L,
  /* 0xce */ set_1_pHL,
  /* 0xcf */ set_1_A,
  /* 0xd0 */ set_2_B,
  /* 0xd1 */ set_2_C,
  /* 0xd2 */ set_2_D,
  /* 0xd3 */ set_2_E,
  /* 0xd4 */ set_2_H,
  /* 0xd5 */ set_2_L,
  /* 0xd6 */ set_2_pHL,
  /* 0xd7 */ set_2_A,
  /* 0xd8 */ set_3_B,
  /* 0xd9 */ set_3_C,
  /* 0xda */ set_3_D,
  /* 0xdb */ set_3_E,
  /* 0xdc */ set_3_H,
  /* 0xdd */ set_3_L,
  /* 0xde */ set_3_pHL,
  /* 0xdf */ set_3_A,
  /* 0xe0 */ set_4_B,
  /* 0xe1 */ set_4_C,
  /* 0xe2 */ set_4_D,
  /* 0xe3 */ set_4_E,
  /* 0xe4 */ set_4_H,
  /* 0xe5 */ set_4_L,
  /* 0xe6 */ set_4_pHL,
  /* 0xe7 */ set_4_A,
  /* 0xe8 */ set_5_B,
  /* 0xe9 */ set_5_C,
  /* 0xea */ set_5_D,
  /* 0xeb */ set_5_E,
  /* 0xec */ set_5_H,
  /* 0xed */ set_5_L,
  /* 0xee */ set_5_pHL,
  /* 0xef */ set_5_A,
  /* 0xf0 */ set_6_B,
  /* 0xf1 */ set_6_C,
  /* 0xf2 */ set_6_D,
  /* 0xf3 */ set_6_E,
  /* 0xf4 */ set_6_H,
  /* 0xf5 */ set_6_L,
  /* 0xf6 */ set_6_pHL,
  /* 0xf7 */ set_6_A,
  /* 0xf8 */ set_7_B,
  /* 0xf9 */ set_7_C,
  /* 0xfa */ set_7_D,
  /* 0xfb */ set_7_E,
  /* 0xfc */ set_7_H,
  /* 0xfd */ set_7_L,
  /* 0xfe */ set_7_pHL,
  /* 0xff */ set_7_A
};

static int cb (void)
{
  ++_regs.RAUX;
  _opcode2= Z80_read ( _regs.PC++ );
  return _insts_cb[_opcode2] ();
}


static int (*const _insts_ddcb[256]) (const Z80s8 desp)=
{
  /* 0x0 */ unk3,
  /* 0x1 */ unk3,
  /* 0x2 */ unk3,
  /* 0x3 */ unk3,
  /* 0x4 */ unk3,
  /* 0x5 */ unk3,
  /* 0x6 */ rlc_pIXd,
  /* 0x7 */ unk3,
  /* 0x8 */ unk3,
  /* 0x9 */ unk3,
  /* 0xa */ unk3,
  /* 0xb */ unk3,
  /* 0xc */ unk3,
  /* 0xd */ unk3,
  /* 0xe */ rrc_pIXd,
  /* 0xf */ unk3,
  /* 0x10 */ unk3,
  /* 0x11 */ unk3,
  /* 0x12 */ unk3,
  /* 0x13 */ unk3,
  /* 0x14 */ unk3,
  /* 0x15 */ unk3,
  /* 0x16 */ rl_pIXd,
  /* 0x17 */ unk3,
  /* 0x18 */ unk3,
  /* 0x19 */ unk3,
  /* 0x1a */ unk3,
  /* 0x1b */ unk3,
  /* 0x1c */ unk3,
  /* 0x1d */ unk3,
  /* 0x1e */ rr_pIXd,
  /* 0x1f */ unk3,
  /* 0x20 */ unk3,
  /* 0x21 */ unk3,
  /* 0x22 */ unk3,
  /* 0x23 */ unk3,
  /* 0x24 */ unk3,
  /* 0x25 */ unk3,
  /* 0x26 */ sla_pIXd,
  /* 0x27 */ unk3,
  /* 0x28 */ unk3,
  /* 0x29 */ unk3,
  /* 0x2a */ unk3,
  /* 0x2b */ unk3,
  /* 0x2c */ unk3,
  /* 0x2d */ unk3,
  /* 0x2e */ sra_pIXd,
  /* 0x2f */ unk3,
  /* 0x30 */ unk3,
  /* 0x31 */ unk3,
  /* 0x32 */ unk3,
  /* 0x33 */ unk3,
  /* 0x34 */ unk3,
  /* 0x35 */ unk3,
  /* 0x36 */ unk3,
  /* 0x37 */ unk3,
  /* 0x38 */ unk3,
  /* 0x39 */ unk3,
  /* 0x3a */ unk3,
  /* 0x3b */ unk3,
  /* 0x3c */ unk3,
  /* 0x3d */ unk3,
  /* 0x3e */ srl_pIXd,
  /* 0x3f */ unk3,
  /* 0x40 */ unk3,
  /* 0x41 */ unk3,
  /* 0x42 */ unk3,
  /* 0x43 */ unk3,
  /* 0x44 */ unk3,
  /* 0x45 */ unk3,
  /* 0x46 */ bit_0_pIXd,
  /* 0x47 */ unk3,
  /* 0x48 */ unk3,
  /* 0x49 */ unk3,
  /* 0x4a */ unk3,
  /* 0x4b */ unk3,
  /* 0x4c */ unk3,
  /* 0x4d */ unk3,
  /* 0x4e */ bit_1_pIXd,
  /* 0x4f */ unk3,
  /* 0x50 */ unk3,
  /* 0x51 */ unk3,
  /* 0x52 */ unk3,
  /* 0x53 */ unk3,
  /* 0x54 */ unk3,
  /* 0x55 */ unk3,
  /* 0x56 */ bit_2_pIXd,
  /* 0x57 */ unk3,
  /* 0x58 */ unk3,
  /* 0x59 */ unk3,
  /* 0x5a */ unk3,
  /* 0x5b */ unk3,
  /* 0x5c */ unk3,
  /* 0x5d */ unk3,
  /* 0x5e */ bit_3_pIXd,
  /* 0x5f */ unk3,
  /* 0x60 */ unk3,
  /* 0x61 */ unk3,
  /* 0x62 */ unk3,
  /* 0x63 */ unk3,
  /* 0x64 */ unk3,
  /* 0x65 */ unk3,
  /* 0x66 */ bit_4_pIXd,
  /* 0x67 */ unk3,
  /* 0x68 */ unk3,
  /* 0x69 */ unk3,
  /* 0x6a */ unk3,
  /* 0x6b */ unk3,
  /* 0x6c */ unk3,
  /* 0x6d */ unk3,
  /* 0x6e */ bit_5_pIXd,
  /* 0x6f */ unk3,
  /* 0x70 */ unk3,
  /* 0x71 */ unk3,
  /* 0x72 */ unk3,
  /* 0x73 */ unk3,
  /* 0x74 */ unk3,
  /* 0x75 */ unk3,
  /* 0x76 */ bit_6_pIXd,
  /* 0x77 */ unk3,
  /* 0x78 */ unk3,
  /* 0x79 */ unk3,
  /* 0x7a */ unk3,
  /* 0x7b */ unk3,
  /* 0x7c */ unk3,
  /* 0x7d */ unk3,
  /* 0x7e */ bit_7_pIXd,
  /* 0x7f */ unk3,
  /* 0x80 */ unk3,
  /* 0x81 */ unk3,
  /* 0x82 */ unk3,
  /* 0x83 */ unk3,
  /* 0x84 */ unk3,
  /* 0x85 */ unk3,
  /* 0x86 */ res_0_pIXd,
  /* 0x87 */ unk3,
  /* 0x88 */ unk3,
  /* 0x89 */ unk3,
  /* 0x8a */ unk3,
  /* 0x8b */ unk3,
  /* 0x8c */ unk3,
  /* 0x8d */ unk3,
  /* 0x8e */ res_1_pIXd,
  /* 0x8f */ unk3,
  /* 0x90 */ unk3,
  /* 0x91 */ unk3,
  /* 0x92 */ unk3,
  /* 0x93 */ unk3,
  /* 0x94 */ unk3,
  /* 0x95 */ unk3,
  /* 0x96 */ res_2_pIXd,
  /* 0x97 */ unk3,
  /* 0x98 */ unk3,
  /* 0x99 */ unk3,
  /* 0x9a */ unk3,
  /* 0x9b */ unk3,
  /* 0x9c */ unk3,
  /* 0x9d */ unk3,
  /* 0x9e */ res_3_pIXd,
  /* 0x9f */ unk3,
  /* 0xa0 */ unk3,
  /* 0xa1 */ unk3,
  /* 0xa2 */ unk3,
  /* 0xa3 */ unk3,
  /* 0xa4 */ unk3,
  /* 0xa5 */ unk3,
  /* 0xa6 */ res_4_pIXd,
  /* 0xa7 */ unk3,
  /* 0xa8 */ unk3,
  /* 0xa9 */ unk3,
  /* 0xaa */ unk3,
  /* 0xab */ unk3,
  /* 0xac */ unk3,
  /* 0xad */ unk3,
  /* 0xae */ res_5_pIXd,
  /* 0xaf */ unk3,
  /* 0xb0 */ unk3,
  /* 0xb1 */ unk3,
  /* 0xb2 */ unk3,
  /* 0xb3 */ unk3,
  /* 0xb4 */ unk3,
  /* 0xb5 */ unk3,
  /* 0xb6 */ res_6_pIXd,
  /* 0xb7 */ unk3,
  /* 0xb8 */ unk3,
  /* 0xb9 */ unk3,
  /* 0xba */ unk3,
  /* 0xbb */ unk3,
  /* 0xbc */ unk3,
  /* 0xbd */ unk3,
  /* 0xbe */ res_7_pIXd,
  /* 0xbf */ unk3,
  /* 0xc0 */ unk3,
  /* 0xc1 */ unk3,
  /* 0xc2 */ unk3,
  /* 0xc3 */ unk3,
  /* 0xc4 */ unk3,
  /* 0xc5 */ unk3,
  /* 0xc6 */ set_0_pIXd,
  /* 0xc7 */ unk3,
  /* 0xc8 */ unk3,
  /* 0xc9 */ unk3,
  /* 0xca */ unk3,
  /* 0xcb */ unk3,
  /* 0xcc */ unk3,
  /* 0xcd */ unk3,
  /* 0xce */ set_1_pIXd,
  /* 0xcf */ unk3,
  /* 0xd0 */ unk3,
  /* 0xd1 */ unk3,
  /* 0xd2 */ unk3,
  /* 0xd3 */ unk3,
  /* 0xd4 */ unk3,
  /* 0xd5 */ unk3,
  /* 0xd6 */ set_2_pIXd,
  /* 0xd7 */ unk3,
  /* 0xd8 */ unk3,
  /* 0xd9 */ unk3,
  /* 0xda */ unk3,
  /* 0xdb */ unk3,
  /* 0xdc */ unk3,
  /* 0xdd */ unk3,
  /* 0xde */ set_3_pIXd,
  /* 0xdf */ unk3,
  /* 0xe0 */ unk3,
  /* 0xe1 */ unk3,
  /* 0xe2 */ unk3,
  /* 0xe3 */ unk3,
  /* 0xe4 */ unk3,
  /* 0xe5 */ unk3,
  /* 0xe6 */ set_4_pIXd,
  /* 0xe7 */ unk3,
  /* 0xe8 */ unk3,
  /* 0xe9 */ unk3,
  /* 0xea */ unk3,
  /* 0xeb */ unk3,
  /* 0xec */ unk3,
  /* 0xed */ unk3,
  /* 0xee */ set_5_pIXd,
  /* 0xef */ unk3,
  /* 0xf0 */ unk3,
  /* 0xf1 */ unk3,
  /* 0xf2 */ unk3,
  /* 0xf3 */ unk3,
  /* 0xf4 */ unk3,
  /* 0xf5 */ unk3,
  /* 0xf6 */ set_6_pIXd,
  /* 0xf7 */ unk3,
  /* 0xf8 */ unk3,
  /* 0xf9 */ unk3,
  /* 0xfa */ unk3,
  /* 0xfb */ unk3,
  /* 0xfc */ unk3,
  /* 0xfd */ unk3,
  /* 0xfe */ set_7_pIXd,
  /* 0xff */ unk3  
};

static int ddcb (void)
{
  Z80s8 desp;
  desp= (Z80s8) Z80_read ( _regs.PC++ );
  _opcode3= Z80_read ( _regs.PC++ );
  return _insts_ddcb[_opcode3] ( desp );
}


static int (*const _insts_dd[256]) (void)=
{
  /* 0x0 */ unk2,
  /* 0x1 */ unk2,
  /* 0x2 */ unk2,
  /* 0x3 */ unk2,
  /* 0x4 */ unk2,
  /* 0x5 */ unk2,
  /* 0x6 */ unk2,
  /* 0x7 */ unk2,
  /* 0x8 */ unk2,
  /* 0x9 */ add_IX_BC,
  /* 0xa */ unk2,
  /* 0xb */ unk2,
  /* 0xc */ unk2,
  /* 0xd */ unk2,
  /* 0xe */ unk2,
  /* 0xf */ unk2,
  /* 0x10 */ unk2,
  /* 0x11 */ unk2,
  /* 0x12 */ unk2,
  /* 0x13 */ unk2,
  /* 0x14 */ unk2,
  /* 0x15 */ unk2,
  /* 0x16 */ unk2,
  /* 0x17 */ unk2,
  /* 0x18 */ unk2,
  /* 0x19 */ add_IX_DE,
  /* 0x1a */ unk2,
  /* 0x1b */ unk2,
  /* 0x1c */ unk2,
  /* 0x1d */ unk2,
  /* 0x1e */ unk2,
  /* 0x1f */ unk2,
  /* 0x20 */ unk2,
  /* 0x21 */ ld_IX_nn,
  /* 0x22 */ ld_pnn_IX,
  /* 0x23 */ inc_IX,
  /* 0x24 */ inc_IXH,
  /* 0x25 */ dec_IXH,
  /* 0x26 */ ld_IXH_n,
  /* 0x27 */ unk2,
  /* 0x28 */ unk2,
  /* 0x29 */ add_IX_IX,
  /* 0x2a */ ld_IX_pnn,
  /* 0x2b */ dec_IX,
  /* 0x2c */ inc_IXL,
  /* 0x2d */ dec_IXL,
  /* 0x2e */ ld_IXL_n,
  /* 0x2f */ unk2,
  /* 0x30 */ unk2,
  /* 0x31 */ unk2,
  /* 0x32 */ unk2,
  /* 0x33 */ unk2,
  /* 0x34 */ inc_pIXd,
  /* 0x35 */ dec_pIXd,
  /* 0x36 */ ld_pIXd_n,
  /* 0x37 */ unk2,
  /* 0x38 */ unk2,
  /* 0x39 */ add_IX_SP,
  /* 0x3a */ unk2,
  /* 0x3b */ unk2,
  /* 0x3c */ unk2,
  /* 0x3d */ unk2,
  /* 0x3e */ unk2,
  /* 0x3f */ unk2,
  /* 0x40 */ unk2,
  /* 0x41 */ unk2,
  /* 0x42 */ unk2,
  /* 0x43 */ unk2,
  /* 0x44 */ ld_B_IXH,
  /* 0x45 */ ld_B_IXL,
  /* 0x46 */ ld_B_pIXd,
  /* 0x47 */ unk2,
  /* 0x48 */ unk2,
  /* 0x49 */ add_IX_BC,
  /* 0x4a */ unk2,
  /* 0x4b */ unk2,
  /* 0x4c */ ld_C_IXH,
  /* 0x4d */ ld_C_IXL,
  /* 0x4e */ ld_C_pIXd,
  /* 0x4f */ unk2,
  /* 0x50 */ unk2,
  /* 0x51 */ unk2,
  /* 0x52 */ unk2,
  /* 0x53 */ unk2,
  /* 0x54 */ ld_D_IXH,
  /* 0x55 */ ld_D_IXL,
  /* 0x56 */ ld_D_pIXd,
  /* 0x57 */ unk2,
  /* 0x58 */ unk2,
  /* 0x59 */ add_IX_DE,
  /* 0x5a */ unk2,
  /* 0x5b */ unk2,
  /* 0x5c */ ld_E_IXH,
  /* 0x5d */ ld_E_IXL,
  /* 0x5e */ ld_E_pIXd,
  /* 0x5f */ unk2,
  /* 0x60 */ ld_IXH_B,
  /* 0x61 */ ld_IXH_C,
  /* 0x62 */ ld_IXH_D,
  /* 0x63 */ ld_IXH_E,
  /* 0x64 */ unk2,
  /* 0x65 */ unk2,
  /* 0x66 */ ld_H_pIXd,
  /* 0x67 */ ld_IXH_A,
  /* 0x68 */ ld_IXL_B,
  /* 0x69 */ ld_IXL_C,
  /* 0x6a */ ld_IXL_D,
  /* 0x6b */ ld_IXL_E,
  /* 0x6c */ unk2,
  /* 0x6d */ unk2,
  /* 0x6e */ ld_L_pIXd,
  /* 0x6f */ ld_IXL_A,
  /* 0x70 */ ld_pIXd_B,
  /* 0x71 */ ld_pIXd_C,
  /* 0x72 */ ld_pIXd_D,
  /* 0x73 */ ld_pIXd_E,
  /* 0x74 */ ld_pIXd_H,
  /* 0x75 */ ld_pIXd_L,
  /* 0x76 */ unk2,
  /* 0x77 */ ld_pIXd_A,
  /* 0x78 */ unk2,
  /* 0x79 */ add_IX_SP,
  /* 0x7a */ unk2,
  /* 0x7b */ unk2,
  /* 0x7c */ ld_A_IXH,
  /* 0x7d */ ld_A_IXL,
  /* 0x7e */ ld_A_pIXd,
  /* 0x7f */ unk2,
  /* 0x80 */ unk2,
  /* 0x81 */ unk2,
  /* 0x82 */ unk2,
  /* 0x83 */ unk2,
  /* 0x84 */ unk2,
  /* 0x85 */ add_A_IXL,
  /* 0x86 */ add_A_pIXd,
  /* 0x87 */ unk2,
  /* 0x88 */ unk2,
  /* 0x89 */ unk2,
  /* 0x8a */ unk2,
  /* 0x8b */ unk2,
  /* 0x8c */ adc_A_IXH,
  /* 0x8d */ unk2,
  /* 0x8e */ adc_A_pIXd,
  /* 0x8f */ unk2,
  /* 0x90 */ unk2,
  /* 0x91 */ unk2,
  /* 0x92 */ unk2,
  /* 0x93 */ unk2,
  /* 0x94 */ sub_A_IXH,
  /* 0x95 */ sub_A_IXL,
  /* 0x96 */ sub_A_pIXd,
  /* 0x97 */ unk2,
  /* 0x98 */ unk2,
  /* 0x99 */ unk2,
  /* 0x9a */ unk2,
  /* 0x9b */ unk2,
  /* 0x9c */ unk2,
  /* 0x9d */ unk2,
  /* 0x9e */ sbc_A_pIXd,
  /* 0x9f */ unk2,
  /* 0xa0 */ unk2,
  /* 0xa1 */ unk2,
  /* 0xa2 */ unk2,
  /* 0xa3 */ unk2,
  /* 0xa4 */ unk2,
  /* 0xa5 */ unk2,
  /* 0xa6 */ and_A_pIXd,
  /* 0xa7 */ unk2,
  /* 0xa8 */ unk2,
  /* 0xa9 */ unk2,
  /* 0xaa */ unk2,
  /* 0xab */ unk2,
  /* 0xac */ unk2,
  /* 0xad */ unk2,
  /* 0xae */ xor_A_pIXd,
  /* 0xaf */ unk2,
  /* 0xb0 */ unk2,
  /* 0xb1 */ unk2,
  /* 0xb2 */ unk2,
  /* 0xb3 */ unk2,
  /* 0xb4 */ or_A_IXH,
  /* 0xb5 */ or_A_IXL,
  /* 0xb6 */ or_A_pIXd,
  /* 0xb7 */ unk2,
  /* 0xb8 */ unk2,
  /* 0xb9 */ unk2,
  /* 0xba */ unk2,
  /* 0xbb */ unk2,
  /* 0xbc */ cp_A_IXH,
  /* 0xbd */ cp_A_IXL,
  /* 0xbe */ cp_A_pIXd,
  /* 0xbf */ unk2,
  /* 0xc0 */ unk2,
  /* 0xc1 */ unk2,
  /* 0xc2 */ unk2,
  /* 0xc3 */ unk2,
  /* 0xc4 */ unk2,
  /* 0xc5 */ unk2,
  /* 0xc6 */ unk2,
  /* 0xc7 */ unk2,
  /* 0xc8 */ unk2,
  /* 0xc9 */ unk2,
  /* 0xca */ unk2,
  /* 0xcb */ ddcb,
  /* 0xcc */ unk2,
  /* 0xcd */ unk2,
  /* 0xce */ unk2,
  /* 0xcf */ unk2,
  /* 0xd0 */ unk2,
  /* 0xd1 */ unk2,
  /* 0xd2 */ unk2,
  /* 0xd3 */ unk2,
  /* 0xd4 */ unk2,
  /* 0xd5 */ unk2,
  /* 0xd6 */ unk2,
  /* 0xd7 */ unk2,
  /* 0xd8 */ unk2,
  /* 0xd9 */ unk2,
  /* 0xda */ unk2,
  /* 0xdb */ unk2,
  /* 0xdc */ unk2,
  /* 0xdd */ unk2,
  /* 0xde */ unk2,
  /* 0xdf */ unk2,
  /* 0xe0 */ unk2,
  /* 0xe1 */ pop_IX,
  /* 0xe2 */ unk2,
  /* 0xe3 */ ex_pSP_IX,
  /* 0xe4 */ unk2,
  /* 0xe5 */ push_IX,
  /* 0xe6 */ unk2,
  /* 0xe7 */ unk2,
  /* 0xe8 */ unk2,
  /* 0xe9 */ jp_IX,
  /* 0xea */ unk2,
  /* 0xeb */ unk2,
  /* 0xec */ unk2,
  /* 0xed */ unk2,
  /* 0xee */ unk2,
  /* 0xef */ unk2,
  /* 0xf0 */ unk2,
  /* 0xf1 */ unk2,
  /* 0xf2 */ unk2,
  /* 0xf3 */ unk2,
  /* 0xf4 */ unk2,
  /* 0xf5 */ unk2,
  /* 0xf6 */ unk2,
  /* 0xf7 */ unk2,
  /* 0xf8 */ unk2,
  /* 0xf9 */ ld_SP_IX,
  /* 0xfa */ unk2,
  /* 0xfb */ unk2,
  /* 0xfc */ unk2,
  /* 0xfd */ unk2,
  /* 0xfe */ unk2,
  /* 0xff */ unk2  
};

static int dd (void)
{
  ++_regs.RAUX;
  _opcode2= Z80_read ( _regs.PC++ );
  return _insts_dd[_opcode2] ();
}


static int (*const _insts_ed[256]) (void)=
{
  /* 0x0 */ unk2,
  /* 0x1 */ unk2,
  /* 0x2 */ unk2,
  /* 0x3 */ unk2,
  /* 0x4 */ unk2,
  /* 0x5 */ unk2,
  /* 0x6 */ unk2,
  /* 0x7 */ unk2,
  /* 0x8 */ unk2,
  /* 0x9 */ unk2,
  /* 0xa */ unk2,
  /* 0xb */ unk2,
  /* 0xc */ unk2,
  /* 0xd */ unk2,
  /* 0xe */ unk2,
  /* 0xf */ unk2,
  /* 0x10 */ unk2,
  /* 0x11 */ unk2,
  /* 0x12 */ unk2,
  /* 0x13 */ unk2,
  /* 0x14 */ unk2,
  /* 0x15 */ unk2,
  /* 0x16 */ unk2,
  /* 0x17 */ unk2,
  /* 0x18 */ unk2,
  /* 0x19 */ unk2,
  /* 0x1a */ unk2,
  /* 0x1b */ unk2,
  /* 0x1c */ unk2,
  /* 0x1d */ unk2,
  /* 0x1e */ unk2,
  /* 0x1f */ unk2,
  /* 0x20 */ unk2,
  /* 0x21 */ unk2,
  /* 0x22 */ unk2,
  /* 0x23 */ unk2,
  /* 0x24 */ unk2,
  /* 0x25 */ unk2,
  /* 0x26 */ unk2,
  /* 0x27 */ unk2,
  /* 0x28 */ unk2,
  /* 0x29 */ unk2,
  /* 0x2a */ unk2,
  /* 0x2b */ unk2,
  /* 0x2c */ unk2,
  /* 0x2d */ unk2,
  /* 0x2e */ unk2,
  /* 0x2f */ unk2,
  /* 0x30 */ unk2,
  /* 0x31 */ unk2,
  /* 0x32 */ unk2,
  /* 0x33 */ ld_pnn_SP,
  /* 0x34 */ unk2,
  /* 0x35 */ unk2,
  /* 0x36 */ unk2,
  /* 0x37 */ unk2,
  /* 0x38 */ unk2,
  /* 0x39 */ unk2,
  /* 0x3a */ unk2,
  /* 0x3b */ unk2,
  /* 0x3c */ unk2,
  /* 0x3d */ unk2,
  /* 0x3e */ unk2,
  /* 0x3f */ unk2,
  /* 0x40 */ in_B_C,
  /* 0x41 */ out_C_B,
  /* 0x42 */ sbc_HL_BC,
  /* 0x43 */ ld_pnn_BC,
  /* 0x44 */ neg,
  /* 0x45 */ retn,
  /* 0x46 */ im0,
  /* 0x47 */ ld_I_A,
  /* 0x48 */ in_C_C,
  /* 0x49 */ out_C_C,
  /* 0x4a */ adc_HL_BC,
  /* 0x4b */ ld_BC_pnn,
  /* 0x4c */ unk2,
  /* 0x4d */ reti,
  /* 0x4e */ unk2,
  /* 0x4f */ ld_R_A,
  /* 0x50 */ in_D_C,
  /* 0x51 */ out_C_D,
  /* 0x52 */ sbc_HL_DE,
  /* 0x53 */ ld_pnn_DE,
  /* 0x54 */ unk2,
  /* 0x55 */ unk2,
  /* 0x56 */ im1,
  /* 0x57 */ ld_A_I,
  /* 0x58 */ in_E_C,
  /* 0x59 */ out_C_E,
  /* 0x5a */ adc_HL_DE,
  /* 0x5b */ ld_DE_pnn,
  /* 0x5c */ unk2,
  /* 0x5d */ unk2,
  /* 0x5e */ im2,
  /* 0x5f */ ld_A_R,
  /* 0x60 */ in_H_C,
  /* 0x61 */ out_C_H,
  /* 0x62 */ sbc_HL_HL,
  /* 0x63 */ ld_pnn_HL_2,
  /* 0x64 */ unk2,
  /* 0x65 */ unk2,
  /* 0x66 */ unk2,
  /* 0x67 */ rrd,
  /* 0x68 */ in_L_C,
  /* 0x69 */ out_C_L,
  /* 0x6a */ adc_HL_HL,
  /* 0x6b */ ld_HL_pnn_2,
  /* 0x6c */ unk2,
  /* 0x6d */ unk2,
  /* 0x6e */ unk2,
  /* 0x6f */ rld,
  /* 0x70 */ in_UNK_C,
  /* 0x71 */ unk2,
  /* 0x72 */ sbc_HL_SP,
  /* 0x73 */ ld_pnn_SP,
  /* 0x74 */ unk2,
  /* 0x75 */ unk2,
  /* 0x76 */ unk2,
  /* 0x77 */ unk2,
  /* 0x78 */ in_A_C,
  /* 0x79 */ out_C_A,
  /* 0x7a */ adc_HL_SP,
  /* 0x7b */ ld_SP_pnn,
  /* 0x7c */ unk2,
  /* 0x7d */ unk2,
  /* 0x7e */ unk2,
  /* 0x7f */ unk2,
  /* 0x80 */ unk2,
  /* 0x81 */ unk2,
  /* 0x82 */ unk2,
  /* 0x83 */ unk2,
  /* 0x84 */ unk2,
  /* 0x85 */ unk2,
  /* 0x86 */ unk2,
  /* 0x87 */ unk2,
  /* 0x88 */ unk2,
  /* 0x89 */ unk2,
  /* 0x8a */ unk2,
  /* 0x8b */ unk2,
  /* 0x8c */ unk2,
  /* 0x8d */ unk2,
  /* 0x8e */ unk2,
  /* 0x8f */ unk2,
  /* 0x90 */ unk2,
  /* 0x91 */ unk2,
  /* 0x92 */ unk2,
  /* 0x93 */ unk2,
  /* 0x94 */ unk2,
  /* 0x95 */ unk2,
  /* 0x96 */ unk2,
  /* 0x97 */ unk2,
  /* 0x98 */ unk2,
  /* 0x99 */ unk2,
  /* 0x9a */ unk2,
  /* 0x9b */ unk2,
  /* 0x9c */ unk2,
  /* 0x9d */ unk2,
  /* 0x9e */ unk2,
  /* 0x9f */ unk2,
  /* 0xa0 */ ldi,
  /* 0xa1 */ cpi,
  /* 0xa2 */ ini,
  /* 0xa3 */ outi,
  /* 0xa4 */ unk2,
  /* 0xa5 */ unk2,
  /* 0xa6 */ unk2,
  /* 0xa7 */ unk2,
  /* 0xa8 */ ldd,
  /* 0xa9 */ cpd,
  /* 0xaa */ ind,
  /* 0xab */ outd,
  /* 0xac */ unk2,
  /* 0xad */ unk2,
  /* 0xae */ unk2,
  /* 0xaf */ unk2,
  /* 0xb0 */ ldir,
  /* 0xb1 */ cpir,
  /* 0xb2 */ inir,
  /* 0xb3 */ otir,
  /* 0xb4 */ unk2,
  /* 0xb5 */ unk2,
  /* 0xb6 */ unk2,
  /* 0xb7 */ unk2,
  /* 0xb8 */ lddr,
  /* 0xb9 */ cpdr,
  /* 0xba */ indr,
  /* 0xbb */ otdr,
  /* 0xbc */ unk2,
  /* 0xbd */ unk2,
  /* 0xbe */ unk2,
  /* 0xbf */ unk2,
  /* 0xc0 */ unk2,
  /* 0xc1 */ unk2,
  /* 0xc2 */ unk2,
  /* 0xc3 */ unk2,
  /* 0xc4 */ unk2,
  /* 0xc5 */ unk2,
  /* 0xc6 */ unk2,
  /* 0xc7 */ unk2,
  /* 0xc8 */ unk2,
  /* 0xc9 */ unk2,
  /* 0xca */ unk2,
  /* 0xcb */ unk2,
  /* 0xcc */ unk2,
  /* 0xcd */ unk2,
  /* 0xce */ unk2,
  /* 0xcf */ unk2,
  /* 0xd0 */ unk2,
  /* 0xd1 */ unk2,
  /* 0xd2 */ unk2,
  /* 0xd3 */ unk2,
  /* 0xd4 */ unk2,
  /* 0xd5 */ unk2,
  /* 0xd6 */ unk2,
  /* 0xd7 */ unk2,
  /* 0xd8 */ unk2,
  /* 0xd9 */ unk2,
  /* 0xda */ unk2,
  /* 0xdb */ unk2,
  /* 0xdc */ unk2,
  /* 0xdd */ unk2,
  /* 0xde */ unk2,
  /* 0xdf */ unk2,
  /* 0xe0 */ unk2,
  /* 0xe1 */ unk2,
  /* 0xe2 */ unk2,
  /* 0xe3 */ unk2,
  /* 0xe4 */ unk2,
  /* 0xe5 */ unk2,
  /* 0xe6 */ unk2,
  /* 0xe7 */ unk2,
  /* 0xe8 */ unk2,
  /* 0xe9 */ unk2,
  /* 0xea */ unk2,
  /* 0xeb */ unk2,
  /* 0xec */ unk2,
  /* 0xed */ unk2,
  /* 0xee */ unk2,
  /* 0xef */ unk2,
  /* 0xf0 */ unk2,
  /* 0xf1 */ unk2,
  /* 0xf2 */ unk2,
  /* 0xf3 */ unk2,
  /* 0xf4 */ unk2,
  /* 0xf5 */ unk2,
  /* 0xf6 */ unk2,
  /* 0xf7 */ unk2,
  /* 0xf8 */ unk2,
  /* 0xf9 */ unk2,
  /* 0xfa */ unk2,
  /* 0xfb */ unk2,
  /* 0xfc */ unk2,
  /* 0xfd */ unk2,
  /* 0xfe */ unk2,
  /* 0xff */ unk2  
};

static int ed (void)
{
  ++_regs.RAUX;
  _opcode2= Z80_read ( _regs.PC++ );
  return _insts_ed[_opcode2] ();
}


static int (*const _insts_fdcb[256]) (const Z80s8 desp)=
{
  /* 0x0 */ unk3,
  /* 0x1 */ unk3,
  /* 0x2 */ unk3,
  /* 0x3 */ unk3,
  /* 0x4 */ unk3,
  /* 0x5 */ unk3,
  /* 0x6 */ rlc_pIYd,
  /* 0x7 */ unk3,
  /* 0x8 */ unk3,
  /* 0x9 */ unk3,
  /* 0xa */ unk3,
  /* 0xb */ unk3,
  /* 0xc */ unk3,
  /* 0xd */ unk3,
  /* 0xe */ rrc_pIYd,
  /* 0xf */ unk3,
  /* 0x10 */ unk3,
  /* 0x11 */ unk3,
  /* 0x12 */ unk3,
  /* 0x13 */ unk3,
  /* 0x14 */ unk3,
  /* 0x15 */ unk3,
  /* 0x16 */ rl_pIYd,
  /* 0x17 */ unk3,
  /* 0x18 */ unk3,
  /* 0x19 */ unk3,
  /* 0x1a */ unk3,
  /* 0x1b */ unk3,
  /* 0x1c */ unk3,
  /* 0x1d */ unk3,
  /* 0x1e */ rr_pIYd,
  /* 0x1f */ unk3,
  /* 0x20 */ unk3,
  /* 0x21 */ unk3,
  /* 0x22 */ unk3,
  /* 0x23 */ unk3,
  /* 0x24 */ unk3,
  /* 0x25 */ unk3,
  /* 0x26 */ sla_pIYd,
  /* 0x27 */ unk3,
  /* 0x28 */ unk3,
  /* 0x29 */ unk3,
  /* 0x2a */ unk3,
  /* 0x2b */ unk3,
  /* 0x2c */ unk3,
  /* 0x2d */ unk3,
  /* 0x2e */ sra_pIYd,
  /* 0x2f */ unk3,
  /* 0x30 */ unk3,
  /* 0x31 */ unk3,
  /* 0x32 */ unk3,
  /* 0x33 */ unk3,
  /* 0x34 */ unk3,
  /* 0x35 */ unk3,
  /* 0x36 */ unk3,
  /* 0x37 */ unk3,
  /* 0x38 */ unk3,
  /* 0x39 */ unk3,
  /* 0x3a */ unk3,
  /* 0x3b */ unk3,
  /* 0x3c */ unk3,
  /* 0x3d */ unk3,
  /* 0x3e */ srl_pIYd,
  /* 0x3f */ unk3,
  /* 0x40 */ unk3,
  /* 0x41 */ unk3,
  /* 0x42 */ unk3,
  /* 0x43 */ unk3,
  /* 0x44 */ unk3,
  /* 0x45 */ unk3,
  /* 0x46 */ bit_0_pIYd,
  /* 0x47 */ unk3,
  /* 0x48 */ unk3,
  /* 0x49 */ unk3,
  /* 0x4a */ unk3,
  /* 0x4b */ unk3,
  /* 0x4c */ unk3,
  /* 0x4d */ unk3,
  /* 0x4e */ bit_1_pIYd,
  /* 0x4f */ unk3,
  /* 0x50 */ unk3,
  /* 0x51 */ unk3,
  /* 0x52 */ unk3,
  /* 0x53 */ unk3,
  /* 0x54 */ unk3,
  /* 0x55 */ unk3,
  /* 0x56 */ bit_2_pIYd,
  /* 0x57 */ unk3,
  /* 0x58 */ unk3,
  /* 0x59 */ unk3,
  /* 0x5a */ unk3,
  /* 0x5b */ unk3,
  /* 0x5c */ unk3,
  /* 0x5d */ unk3,
  /* 0x5e */ bit_3_pIYd,
  /* 0x5f */ unk3,
  /* 0x60 */ unk3,
  /* 0x61 */ unk3,
  /* 0x62 */ unk3,
  /* 0x63 */ unk3,
  /* 0x64 */ unk3,
  /* 0x65 */ unk3,
  /* 0x66 */ bit_4_pIYd,
  /* 0x67 */ unk3,
  /* 0x68 */ unk3,
  /* 0x69 */ unk3,
  /* 0x6a */ unk3,
  /* 0x6b */ unk3,
  /* 0x6c */ unk3,
  /* 0x6d */ unk3,
  /* 0x6e */ bit_5_pIYd,
  /* 0x6f */ unk3,
  /* 0x70 */ unk3,
  /* 0x71 */ unk3,
  /* 0x72 */ unk3,
  /* 0x73 */ unk3,
  /* 0x74 */ unk3,
  /* 0x75 */ unk3,
  /* 0x76 */ bit_6_pIYd,
  /* 0x77 */ unk3,
  /* 0x78 */ unk3,
  /* 0x79 */ unk3,
  /* 0x7a */ unk3,
  /* 0x7b */ unk3,
  /* 0x7c */ unk3,
  /* 0x7d */ unk3,
  /* 0x7e */ bit_7_pIYd,
  /* 0x7f */ unk3,
  /* 0x80 */ unk3,
  /* 0x81 */ unk3,
  /* 0x82 */ unk3,
  /* 0x83 */ unk3,
  /* 0x84 */ unk3,
  /* 0x85 */ unk3,
  /* 0x86 */ res_0_pIYd,
  /* 0x87 */ unk3,
  /* 0x88 */ unk3,
  /* 0x89 */ unk3,
  /* 0x8a */ unk3,
  /* 0x8b */ unk3,
  /* 0x8c */ unk3,
  /* 0x8d */ unk3,
  /* 0x8e */ res_1_pIYd,
  /* 0x8f */ unk3,
  /* 0x90 */ unk3,
  /* 0x91 */ unk3,
  /* 0x92 */ unk3,
  /* 0x93 */ unk3,
  /* 0x94 */ unk3,
  /* 0x95 */ unk3,
  /* 0x96 */ res_2_pIYd,
  /* 0x97 */ unk3,
  /* 0x98 */ unk3,
  /* 0x99 */ unk3,
  /* 0x9a */ unk3,
  /* 0x9b */ unk3,
  /* 0x9c */ unk3,
  /* 0x9d */ unk3,
  /* 0x9e */ res_3_pIYd,
  /* 0x9f */ unk3,
  /* 0xa0 */ unk3,
  /* 0xa1 */ unk3,
  /* 0xa2 */ unk3,
  /* 0xa3 */ unk3,
  /* 0xa4 */ unk3,
  /* 0xa5 */ unk3,
  /* 0xa6 */ res_4_pIYd,
  /* 0xa7 */ unk3,
  /* 0xa8 */ unk3,
  /* 0xa9 */ unk3,
  /* 0xaa */ unk3,
  /* 0xab */ unk3,
  /* 0xac */ unk3,
  /* 0xad */ unk3,
  /* 0xae */ res_5_pIYd,
  /* 0xaf */ unk3,
  /* 0xb0 */ unk3,
  /* 0xb1 */ unk3,
  /* 0xb2 */ unk3,
  /* 0xb3 */ unk3,
  /* 0xb4 */ unk3,
  /* 0xb5 */ unk3,
  /* 0xb6 */ res_6_pIYd,
  /* 0xb7 */ unk3,
  /* 0xb8 */ unk3,
  /* 0xb9 */ unk3,
  /* 0xba */ unk3,
  /* 0xbb */ unk3,
  /* 0xbc */ unk3,
  /* 0xbd */ unk3,
  /* 0xbe */ res_7_pIYd,
  /* 0xbf */ unk3,
  /* 0xc0 */ unk3,
  /* 0xc1 */ unk3,
  /* 0xc2 */ unk3,
  /* 0xc3 */ unk3,
  /* 0xc4 */ unk3,
  /* 0xc5 */ unk3,
  /* 0xc6 */ set_0_pIYd,
  /* 0xc7 */ unk3,
  /* 0xc8 */ unk3,
  /* 0xc9 */ unk3,
  /* 0xca */ unk3,
  /* 0xcb */ unk3,
  /* 0xcc */ unk3,
  /* 0xcd */ unk3,
  /* 0xce */ set_1_pIYd,
  /* 0xcf */ unk3,
  /* 0xd0 */ unk3,
  /* 0xd1 */ unk3,
  /* 0xd2 */ unk3,
  /* 0xd3 */ unk3,
  /* 0xd4 */ unk3,
  /* 0xd5 */ unk3,
  /* 0xd6 */ set_2_pIYd,
  /* 0xd7 */ unk3,
  /* 0xd8 */ unk3,
  /* 0xd9 */ unk3,
  /* 0xda */ unk3,
  /* 0xdb */ unk3,
  /* 0xdc */ unk3,
  /* 0xdd */ unk3,
  /* 0xde */ set_3_pIYd,
  /* 0xdf */ unk3,
  /* 0xe0 */ unk3,
  /* 0xe1 */ unk3,
  /* 0xe2 */ unk3,
  /* 0xe3 */ unk3,
  /* 0xe4 */ unk3,
  /* 0xe5 */ unk3,
  /* 0xe6 */ set_4_pIYd,
  /* 0xe7 */ unk3,
  /* 0xe8 */ unk3,
  /* 0xe9 */ unk3,
  /* 0xea */ unk3,
  /* 0xeb */ unk3,
  /* 0xec */ unk3,
  /* 0xed */ unk3,
  /* 0xee */ set_5_pIYd,
  /* 0xef */ unk3,
  /* 0xf0 */ unk3,
  /* 0xf1 */ unk3,
  /* 0xf2 */ unk3,
  /* 0xf3 */ unk3,
  /* 0xf4 */ unk3,
  /* 0xf5 */ unk3,
  /* 0xf6 */ set_6_pIYd,
  /* 0xf7 */ unk3,
  /* 0xf8 */ unk3,
  /* 0xf9 */ unk3,
  /* 0xfa */ unk3,
  /* 0xfb */ unk3,
  /* 0xfc */ unk3,
  /* 0xfd */ unk3,
  /* 0xfe */ set_7_pIYd,
  /* 0xff */ unk3  
};

static int fdcb (void)
{
  Z80s8 desp;
  desp= (Z80s8) Z80_read ( _regs.PC++ );
  _opcode3= Z80_read ( _regs.PC++ );
  return _insts_fdcb[_opcode3] ( desp );
}


static int (*const _insts_fd[256]) (void)=
{
  /* 0x0 */ unk2,
  /* 0x1 */ unk2,
  /* 0x2 */ unk2,
  /* 0x3 */ unk2,
  /* 0x4 */ unk2,
  /* 0x5 */ unk2,
  /* 0x6 */ unk2,
  /* 0x7 */ unk2,
  /* 0x8 */ unk2,
  /* 0x9 */ add_IY_BC,
  /* 0xa */ unk2,
  /* 0xb */ unk2,
  /* 0xc */ unk2,
  /* 0xd */ unk2,
  /* 0xe */ unk2,
  /* 0xf */ unk2,
  /* 0x10 */ unk2,
  /* 0x11 */ unk2,
  /* 0x12 */ unk2,
  /* 0x13 */ unk2,
  /* 0x14 */ unk2,
  /* 0x15 */ unk2,
  /* 0x16 */ unk2,
  /* 0x17 */ unk2,
  /* 0x18 */ unk2,
  /* 0x19 */ add_IY_DE,
  /* 0x1a */ unk2,
  /* 0x1b */ unk2,
  /* 0x1c */ unk2,
  /* 0x1d */ unk2,
  /* 0x1e */ unk2,
  /* 0x1f */ unk2,
  /* 0x20 */ unk2,
  /* 0x21 */ ld_IY_nn,
  /* 0x22 */ ld_pnn_IY,
  /* 0x23 */ inc_IY,
  /* 0x24 */ inc_IYH,
  /* 0x25 */ dec_IYH,
  /* 0x26 */ ld_IYH_n,
  /* 0x27 */ unk2,
  /* 0x28 */ unk2,
  /* 0x29 */ add_IY_IY,
  /* 0x2a */ ld_IY_pnn,
  /* 0x2b */ dec_IY,
  /* 0x2c */ inc_IYL,
  /* 0x2d */ dec_IYL,
  /* 0x2e */ ld_IYL_n,
  /* 0x2f */ unk2,
  /* 0x30 */ unk2,
  /* 0x31 */ unk2,
  /* 0x32 */ unk2,
  /* 0x33 */ unk2,
  /* 0x34 */ inc_pIYd,
  /* 0x35 */ dec_pIYd,
  /* 0x36 */ ld_pIYd_n,
  /* 0x37 */ unk2,
  /* 0x38 */ unk2,
  /* 0x39 */ add_IY_SP,
  /* 0x3a */ unk2,
  /* 0x3b */ unk2,
  /* 0x3c */ unk2,
  /* 0x3d */ unk2,
  /* 0x3e */ unk2,
  /* 0x3f */ unk2,
  /* 0x40 */ unk2,
  /* 0x41 */ unk2,
  /* 0x42 */ unk2,
  /* 0x43 */ unk2,
  /* 0x44 */ ld_B_IYH,
  /* 0x45 */ ld_B_IYL,
  /* 0x46 */ ld_B_pIYd,
  /* 0x47 */ unk2,
  /* 0x48 */ unk2,
  /* 0x49 */ unk2,
  /* 0x4a */ unk2,
  /* 0x4b */ unk2,
  /* 0x4c */ ld_C_IYH,
  /* 0x4d */ ld_C_IYL,
  /* 0x4e */ ld_C_pIYd,
  /* 0x4f */ unk2,
  /* 0x50 */ unk2,
  /* 0x51 */ unk2,
  /* 0x52 */ unk2,
  /* 0x53 */ unk2,
  /* 0x54 */ ld_D_IYH,
  /* 0x55 */ ld_D_IYL,
  /* 0x56 */ ld_D_pIYd,
  /* 0x57 */ unk2,
  /* 0x58 */ unk2,
  /* 0x59 */ unk2,
  /* 0x5a */ unk2,
  /* 0x5b */ unk2,
  /* 0x5c */ ld_E_IYH,
  /* 0x5d */ ld_E_IYL,
  /* 0x5e */ ld_E_pIYd,
  /* 0x5f */ unk2,
  /* 0x60 */ ld_IYH_B,
  /* 0x61 */ ld_IYH_C,
  /* 0x62 */ ld_IYH_D,
  /* 0x63 */ ld_IYH_E,
  /* 0x64 */ unk2,
  /* 0x65 */ unk2,
  /* 0x66 */ ld_H_pIYd,
  /* 0x67 */ ld_IYH_A,
  /* 0x68 */ ld_IYL_B,
  /* 0x69 */ ld_IYL_C,
  /* 0x6a */ ld_IYL_D,
  /* 0x6b */ ld_IYL_E,
  /* 0x6c */ unk2,
  /* 0x6d */ unk2,
  /* 0x6e */ ld_L_pIYd,
  /* 0x6f */ ld_IYL_A,
  /* 0x70 */ ld_pIYd_B,
  /* 0x71 */ ld_pIYd_C,
  /* 0x72 */ ld_pIYd_D,
  /* 0x73 */ ld_pIYd_E,
  /* 0x74 */ ld_pIYd_H,
  /* 0x75 */ ld_pIYd_L,
  /* 0x76 */ unk2,
  /* 0x77 */ ld_pIYd_A,
  /* 0x78 */ unk2,
  /* 0x79 */ unk2,
  /* 0x7a */ unk2,
  /* 0x7b */ unk2,
  /* 0x7c */ ld_A_IYH,
  /* 0x7d */ ld_A_IYL,
  /* 0x7e */ ld_A_pIYd,
  /* 0x7f */ unk2,
  /* 0x80 */ unk2,
  /* 0x81 */ unk2,
  /* 0x82 */ unk2,
  /* 0x83 */ unk2,
  /* 0x84 */ unk2,
  /* 0x85 */ add_A_IYL,
  /* 0x86 */ add_A_pIYd,
  /* 0x87 */ unk2,
  /* 0x88 */ unk2,
  /* 0x89 */ unk2,
  /* 0x8a */ unk2,
  /* 0x8b */ unk2,
  /* 0x8c */ adc_A_IYH,
  /* 0x8d */ unk2,
  /* 0x8e */ adc_A_pIYd,
  /* 0x8f */ unk2,
  /* 0x90 */ unk2,
  /* 0x91 */ unk2,
  /* 0x92 */ unk2,
  /* 0x93 */ unk2,
  /* 0x94 */ sub_A_IYH,
  /* 0x95 */ sub_A_IYL,
  /* 0x96 */ sub_A_pIYd,
  /* 0x97 */ unk2,
  /* 0x98 */ unk2,
  /* 0x99 */ unk2,
  /* 0x9a */ unk2,
  /* 0x9b */ unk2,
  /* 0x9c */ unk2,
  /* 0x9d */ unk2,
  /* 0x9e */ sbc_A_pIYd,
  /* 0x9f */ unk2,
  /* 0xa0 */ unk2,
  /* 0xa1 */ unk2,
  /* 0xa2 */ unk2,
  /* 0xa3 */ unk2,
  /* 0xa4 */ unk2,
  /* 0xa5 */ unk2,
  /* 0xa6 */ and_A_pIYd,
  /* 0xa7 */ unk2,
  /* 0xa8 */ unk2,
  /* 0xa9 */ unk2,
  /* 0xaa */ unk2,
  /* 0xab */ unk2,
  /* 0xac */ unk2,
  /* 0xad */ unk2,
  /* 0xae */ xor_A_pIYd,
  /* 0xaf */ unk2,
  /* 0xb0 */ unk2,
  /* 0xb1 */ unk2,
  /* 0xb2 */ unk2,
  /* 0xb3 */ unk2,
  /* 0xb4 */ or_A_IYH,
  /* 0xb5 */ or_A_IYL,
  /* 0xb6 */ or_A_pIYd,
  /* 0xb7 */ unk2,
  /* 0xb8 */ unk2,
  /* 0xb9 */ unk2,
  /* 0xba */ unk2,
  /* 0xbb */ unk2,
  /* 0xbc */ cp_A_IYH,
  /* 0xbd */ cp_A_IYL,
  /* 0xbe */ cp_A_pIYd,
  /* 0xbf */ unk2,
  /* 0xc0 */ unk2,
  /* 0xc1 */ unk2,
  /* 0xc2 */ unk2,
  /* 0xc3 */ unk2,
  /* 0xc4 */ unk2,
  /* 0xc5 */ unk2,
  /* 0xc6 */ unk2,
  /* 0xc7 */ unk2,
  /* 0xc8 */ unk2,
  /* 0xc9 */ unk2,
  /* 0xca */ unk2,
  /* 0xcb */ fdcb,
  /* 0xcc */ unk2,
  /* 0xcd */ unk2,
  /* 0xce */ unk2,
  /* 0xcf */ unk2,
  /* 0xd0 */ unk2,
  /* 0xd1 */ unk2,
  /* 0xd2 */ unk2,
  /* 0xd3 */ unk2,
  /* 0xd4 */ unk2,
  /* 0xd5 */ unk2,
  /* 0xd6 */ unk2,
  /* 0xd7 */ unk2,
  /* 0xd8 */ unk2,
  /* 0xd9 */ unk2,
  /* 0xda */ unk2,
  /* 0xdb */ unk2,
  /* 0xdc */ unk2,
  /* 0xdd */ unk2,
  /* 0xde */ unk2,
  /* 0xdf */ unk2,
  /* 0xe0 */ unk2,
  /* 0xe1 */ pop_IY,
  /* 0xe2 */ unk2,
  /* 0xe3 */ ex_pSP_IY,
  /* 0xe4 */ unk2,
  /* 0xe5 */ push_IY,
  /* 0xe6 */ unk2,
  /* 0xe7 */ unk2,
  /* 0xe8 */ unk2,
  /* 0xe9 */ jp_IY,
  /* 0xea */ unk2,
  /* 0xeb */ unk2,
  /* 0xec */ unk2,
  /* 0xed */ unk2,
  /* 0xee */ unk2,
  /* 0xef */ unk2,
  /* 0xf0 */ unk2,
  /* 0xf1 */ unk2,
  /* 0xf2 */ unk2,
  /* 0xf3 */ unk2,
  /* 0xf4 */ unk2,
  /* 0xf5 */ unk2,
  /* 0xf6 */ unk2,
  /* 0xf7 */ unk2,
  /* 0xf8 */ unk2,
  /* 0xf9 */ ld_SP_IY,
  /* 0xfa */ unk2,
  /* 0xfb */ unk2,
  /* 0xfc */ unk2,
  /* 0xfd */ unk2,
  /* 0xfe */ unk2,
  /* 0xff */ unk2  
};

static int fd (void)
{
  ++_regs.RAUX;
  _opcode2= Z80_read ( _regs.PC++ );
  return _insts_fd[_opcode2] ();
}


static int (*const _insts[256]) (void)=
{
  /* 0x00 */ nop,
  /* 0x01 */ ld_BC_nn,
  /* 0x02 */ ld_pBC_A,
  /* 0x03 */ inc_BC,
  /* 0x04 */ inc_B,
  /* 0x05 */ dec_B,
  /* 0x06 */ ld_B_n,
  /* 0x07 */ rlca,
  /* 0x08 */ ex_AF_AF2,
  /* 0x09 */ add_HL_BC,
  /* 0x0a */ ld_A_pBC,
  /* 0x0b */ dec_BC,
  /* 0x0c */ inc_C,
  /* 0x0d */ dec_C,
  /* 0x0e */ ld_C_n,
  /* 0x0f */ rrca,
  /* 0x10 */ djnz,
  /* 0x11 */ ld_DE_nn,
  /* 0x12 */ ld_pDE_A,
  /* 0x13 */ inc_DE,
  /* 0x14 */ inc_D,
  /* 0x15 */ dec_D,
  /* 0x16 */ ld_D_n,
  /* 0x17 */ rla,
  /* 0x18 */ jr,
  /* 0x19 */ add_HL_DE,
  /* 0x1a */ ld_A_pDE,
  /* 0x1b */ dec_DE,
  /* 0x1c */ inc_E,
  /* 0x1d */ dec_E,
  /* 0x1e */ ld_E_n,
  /* 0x1f */ rra,
  /* 0x20 */ jr_NZ,
  /* 0x21 */ ld_HL_nn,
  /* 0x22 */ ld_pnn_HL,
  /* 0x23 */ inc_HL,
  /* 0x24 */ inc_H,
  /* 0x25 */ dec_H,
  /* 0x26 */ ld_H_n,
  /* 0x27 */ daa,
  /* 0x28 */ jr_Z,
  /* 0x29 */ add_HL_HL,
  /* 0x2a */ ld_HL_pnn,
  /* 0x2b */ dec_HL,
  /* 0x2c */ inc_L,
  /* 0x2d */ dec_L,
  /* 0x2e */ ld_L_n,
  /* 0x2f */ cpl,
  /* 0x30 */ jr_NC,
  /* 0x31 */ ld_SP_nn,
  /* 0x32 */ ld_pnn_A,
  /* 0x33 */ inc_SP,
  /* 0x34 */ inc_pHL,
  /* 0x35 */ dec_pHL,
  /* 0x36 */ ld_pHL_n,
  /* 0x37 */ scf,
  /* 0x38 */ jr_C,
  /* 0x39 */ add_HL_SP,
  /* 0x3a */ ld_A_pnn,
  /* 0x3b */ dec_SP,
  /* 0x3c */ inc_A,
  /* 0x3d */ dec_A,
  /* 0x3e */ ld_A_n,
  /* 0x3f */ ccf,
  /* 0x40 */ ld_B_B,
  /* 0x41 */ ld_B_C,
  /* 0x42 */ ld_B_D,
  /* 0x43 */ ld_B_E,
  /* 0x44 */ ld_B_H,
  /* 0x45 */ ld_B_L,
  /* 0x46 */ ld_B_pHL,
  /* 0x47 */ ld_B_A,
  /* 0x48 */ ld_C_B,
  /* 0x49 */ ld_C_C,
  /* 0x4a */ ld_C_D,
  /* 0x4b */ ld_C_E,
  /* 0x4c */ ld_C_H,
  /* 0x4d */ ld_C_L,
  /* 0x4e */ ld_C_pHL,
  /* 0x4f */ ld_C_A,
  /* 0x50 */ ld_D_B,
  /* 0x51 */ ld_D_C,
  /* 0x52 */ ld_D_D,
  /* 0x53 */ ld_D_E,
  /* 0x54 */ ld_D_H,
  /* 0x55 */ ld_D_L,
  /* 0x56 */ ld_D_pHL,
  /* 0x57 */ ld_D_A,
  /* 0x58 */ ld_E_B,
  /* 0x59 */ ld_E_C,
  /* 0x5a */ ld_E_D,
  /* 0x5b */ ld_E_E,
  /* 0x5c */ ld_E_H,
  /* 0x5d */ ld_E_L,
  /* 0x5e */ ld_E_pHL,
  /* 0x5f */ ld_E_A,
  /* 0x60 */ ld_H_B,
  /* 0x61 */ ld_H_C,
  /* 0x62 */ ld_H_D,
  /* 0x63 */ ld_H_E,
  /* 0x64 */ ld_H_H,
  /* 0x65 */ ld_H_L,
  /* 0x66 */ ld_H_pHL,
  /* 0x67 */ ld_H_A,
  /* 0x68 */ ld_L_B,
  /* 0x69 */ ld_L_C,
  /* 0x6a */ ld_L_D,
  /* 0x6b */ ld_L_E,
  /* 0x6c */ ld_L_H,
  /* 0x6d */ ld_L_L,
  /* 0x6e */ ld_L_pHL,
  /* 0x6f */ ld_L_A,
  /* 0x70 */ ld_pHL_B,
  /* 0x71 */ ld_pHL_C,
  /* 0x72 */ ld_pHL_D,
  /* 0x73 */ ld_pHL_E,
  /* 0x74 */ ld_pHL_H,
  /* 0x75 */ ld_pHL_L,
  /* 0x76 */ halt,
  /* 0x77 */ ld_pHL_A,
  /* 0x78 */ ld_A_B,
  /* 0x79 */ ld_A_C,
  /* 0x7a */ ld_A_D,
  /* 0x7b */ ld_A_E,
  /* 0x7c */ ld_A_H,
  /* 0x7d */ ld_A_L,
  /* 0x7e */ ld_A_pHL,
  /* 0x7f */ ld_A_A,
  /* 0x80 */ add_A_B,
  /* 0x81 */ add_A_C,
  /* 0x82 */ add_A_D,
  /* 0x83 */ add_A_E,
  /* 0x84 */ add_A_H,
  /* 0x85 */ add_A_L,
  /* 0x86 */ add_A_pHL,
  /* 0x87 */ add_A_A,
  /* 0x88 */ adc_A_B,
  /* 0x89 */ adc_A_C,
  /* 0x8a */ adc_A_D,
  /* 0x8b */ adc_A_E,
  /* 0x8c */ adc_A_H,
  /* 0x8d */ adc_A_L,
  /* 0x8e */ adc_A_pHL,
  /* 0x8f */ adc_A_A,
  /* 0x90 */ sub_A_B,
  /* 0x91 */ sub_A_C,
  /* 0x92 */ sub_A_D,
  /* 0x93 */ sub_A_E,
  /* 0x94 */ sub_A_H,
  /* 0x95 */ sub_A_L,
  /* 0x96 */ sub_A_pHL,
  /* 0x97 */ sub_A_A,
  /* 0x98 */ sbc_A_B,
  /* 0x99 */ sbc_A_C,
  /* 0x9a */ sbc_A_D,
  /* 0x9b */ sbc_A_E,
  /* 0x9c */ sbc_A_H,
  /* 0x9d */ sbc_A_L,
  /* 0x9e */ sbc_A_pHL,
  /* 0x9f */ sbc_A_A,
  /* 0xa0 */ and_A_B,
  /* 0xa1 */ and_A_C,
  /* 0xa2 */ and_A_D,
  /* 0xa3 */ and_A_E,
  /* 0xa4 */ and_A_H,
  /* 0xa5 */ and_A_L,
  /* 0xa6 */ and_A_pHL,
  /* 0xa7 */ and_A_A,
  /* 0xa8 */ xor_A_B,
  /* 0xa9 */ xor_A_C,
  /* 0xaa */ xor_A_D,
  /* 0xab */ xor_A_E,
  /* 0xac */ xor_A_H,
  /* 0xad */ xor_A_L,
  /* 0xae */ xor_A_pHL,
  /* 0xaf */ xor_A_A,
  /* 0xb0 */ or_A_B,
  /* 0xb1 */ or_A_C,
  /* 0xb2 */ or_A_D,
  /* 0xb3 */ or_A_E,
  /* 0xb4 */ or_A_H,
  /* 0xb5 */ or_A_L,
  /* 0xb6 */ or_A_pHL,
  /* 0xb7 */ or_A_A,
  /* 0xb8 */ cp_A_B,
  /* 0xb9 */ cp_A_C,
  /* 0xba */ cp_A_D,
  /* 0xbb */ cp_A_E,
  /* 0xbc */ cp_A_H,
  /* 0xbd */ cp_A_L,
  /* 0xbe */ cp_A_pHL,
  /* 0xbf */ cp_A_A,
  /* 0xc0 */ ret_NZ,
  /* 0xc1 */ pop_BC,
  /* 0xc2 */ jp_NZ,
  /* 0xc3 */ jp,
  /* 0xc4 */ call_NZ,
  /* 0xc5 */ push_BC,
  /* 0xc6 */ add_A_n,
  /* 0xc7 */ rst_00,
  /* 0xc8 */ ret_Z,
  /* 0xc9 */ ret,
  /* 0xca */ jp_Z,
  /* 0xcb */ cb,
  /* 0xcc */ call_Z,
  /* 0xcd */ call,
  /* 0xce */ adc_A_n,
  /* 0xcf */ rst_08,
  /* 0xd0 */ ret_NC,
  /* 0xd1 */ pop_DE,
  /* 0xd2 */ jp_NC,
  /* 0xd3 */ out_n_A,
  /* 0xd4 */ call_NC,
  /* 0xd5 */ push_DE,
  /* 0xd6 */ sub_A_n,
  /* 0xd7 */ rst_10,
  /* 0xd8 */ ret_C,
  /* 0xd9 */ exx,
  /* 0xda */ jp_C,
  /* 0xdb */ in_A_n,
  /* 0xdc */ call_C,
  /* 0xdd */ dd,
  /* 0xde */ sbc_A_n,
  /* 0xdf */ rst_18,
  /* 0xe0 */ ret_PO,
  /* 0xe1 */ pop_HL,
  /* 0xe2 */ jp_PO,
  /* 0xe3 */ ex_pSP_HL,
  /* 0xe4 */ call_PO,
  /* 0xe5 */ push_HL,
  /* 0xe6 */ and_A_n,
  /* 0xe7 */ rst_20,
  /* 0xe8 */ ret_PE,
  /* 0xe9 */ jp_HL,
  /* 0xea */ jp_PE,
  /* 0xeb */ ex_DE_HL,
  /* 0xec */ call_PE,
  /* 0xed */ ed,
  /* 0xee */ xor_A_n,
  /* 0xef */ rst_28,
  /* 0xf0 */ ret_P,
  /* 0xf1 */ pop_AF,
  /* 0xf2 */ jp_P,
  /* 0xf3 */ di,
  /* 0xf4 */ call_P,
  /* 0xf5 */ push_AF,
  /* 0xf6 */ or_A_n,
  /* 0xf7 */ rst_30,
  /* 0xf8 */ ret_M,
  /* 0xf9 */ ld_SP_HL,
  /* 0xfa */ jp_M,
  /* 0xfb */ ei,
  /* 0xfc */ call_M,
  /* 0xfd */ fd,
  /* 0xfe */ cp_A_n,
  /* 0xff */ rst_38
};


static int
nmi (void)
{
  
  _regs.unhalted= Z80_TRUE;
  _interruptions.flags^= NMI; /* Aquesta interrupció és un puls. */
  PUSH_PC;
  _regs.PC= 0x66;
  _regs.IFF1= Z80_FALSE;
  
  /* No se que passa quan es produeïx una interrupció NMI justetament
     després d'una EI. Crec que el següent codi és el més llògic. */
  if ( _regs.enable_IFF )
    {
      _regs.enable_IFF= Z80_FALSE;
      _regs.IFF2= Z80_TRUE;
    }
  
  return 11;
  
} /* end nmi */


static int
irq (void)
{
  
  Z80u16 addr;
  
  
  /* Aquesta interrupció és un estat. */
  _regs.unhalted= Z80_TRUE;
  _regs.IFF1= _regs.IFF2= Z80_FALSE;
  
  switch ( _regs.imode )
    {
    case 0: return _insts[_interruptions.bus] () + 2;
    case 1:
      PUSH_PC;
      _regs.PC= 0x38;
      return 13;
    case 2:
      PUSH_PC;
      addr= (_interruptions.bus&0xFE) | (((Z80u16) _regs.I)<<8);
      _regs.PC= Z80_read ( addr );
      _regs.PC|= ((Z80u16) Z80_read ( addr|1 ))<<8;
      return 19;
    default: return 0; /* Evitar queixes. */
    }
  
} /* end irq */




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

Z80u16
Z80_decode_next_step (
        	      Z80_Step *step
        	      )
{
  
  Z80u16 addr, ret;
  
  
  if ( _interruptions.flags )
    {
      if ( _interruptions.flags&NMI )
        {
          step->type= Z80_STEP_NMI;
          return 0x66;
        }
      else if ( !_regs.enable_IFF && _regs.IFF1
        	/* && _interruptions.flags&IRQ*/)
        {
          step->type= Z80_STEP_IRQ;
          step->val.bus= _interruptions.bus;
          switch ( _regs.imode )
            {
            case 0:
              _warning ( _udata, "no es pot saber l'adreça de tornada"
        		 " de la interrupció en mode 0 sense executar"
        		 " la instrucció" );
              return 0x00;
            case 1: return 0x38;
            case 2:
              addr= (_interruptions.bus&0xFE) | (((Z80u16) _regs.I)<<8);
              ret= Z80_read ( addr );
              ret|= ((Z80u16) Z80_read ( addr|1 ))<<8;
              return ret;
            default: return 0; /* Evitar queixes. */
            }
        }
    }
  step->type= Z80_STEP_INST;
  return Z80_decode ( _regs.PC, &(step->val.inst) );
  
} /* end Z80_decode_next_step */


void
Z80_init (
          Z80_Warning *warning,
          void        *udata
          )
{
  
  _warning= warning;
  _udata= udata;
  Z80_init_state ();
  
} /* end Z80_init */


void
Z80_init_state (void)
{
  
  _opcode= _opcode2= _opcode3= 0;
  
  /* Registres. */
  _regs.A= _regs.A2= 0;
  _regs.F= _regs.F2= 0;
  _regs.B= _regs.B2= 0;
  _regs.C= _regs.C2= 0;
  _regs.D= _regs.D2= 0;
  _regs.E= _regs.E2= 0;
  _regs.H= _regs.H2= 0;
  _regs.L= _regs.L2= 0;
  _regs.I= 0;
  _regs.R= _regs.RAUX= 0;
  _regs.IX= 0;
  _regs.IY= 0;
  _regs.SP= 0;
  _regs.PC= 0;
  _regs.imode= 0;
  _regs.IFF1= _regs.IFF2= Z80_FALSE;
  _regs.halted= Z80_FALSE;
  _regs.unhalted= Z80_FALSE;
  _regs.enable_IFF= Z80_FALSE;
  
  /* Interrupcions. */
  _interruptions.flags= 0;
  _interruptions.bus= 0xc7;
  
} /* end Z80_init_state */


void
Z80_IRQ (
         const Z80_Bool active,
         const Z80u8    data
         )
{
  
  if ( active )
    {
      _interruptions.flags|= IRQ;
      _interruptions.bus= data;
    }
  else _interruptions.flags&= ~IRQ;
  
} /* end Z80_IRQ */


void
Z80_NMI (void)
{
  _interruptions.flags|= NMI;
} /* end Z80_NMI */


void
Z80_reset (void)
{
  
  /* Altres. Me ho estic inventant. */
  _regs.imode= 0;
  _regs.IFF1= _regs.IFF2= Z80_FALSE;
  _regs.halted= Z80_FALSE;
  _regs.unhalted= Z80_FALSE;
  _regs.enable_IFF= Z80_FALSE;
  _interruptions.flags= 0;
  _interruptions.bus= 0xc7;
  
  /* Comptador de programa. */
  _regs.PC= 0;
  
} /* end Z80_reset */


int
Z80_run (void)
{
  
  /* És important que estiga abans de la comprovació de
     'enable_IFF'. Vore la funció 'nmi' per a més detall. */
  /* Després de una instrucció EI no es pot porduïr una interrupció
     IRQ. */
  if ( _interruptions.flags )
    {
      if ( _interruptions.flags&NMI ) return nmi ();
      else if ( !_regs.enable_IFF && _regs.IFF1
        	/* && _interruptions.flags&IRQ*/ ) return irq ();
    }
  if ( _regs.enable_IFF )
    {
      _regs.enable_IFF= Z80_FALSE;
      _regs.IFF1= _regs.IFF2= Z80_TRUE;
    }
  ++_regs.RAUX;
  _opcode= Z80_read ( _regs.PC++ );
  return _insts[_opcode] ();
  
} /* end Z80_run */


int
Z80_load_state (
        	FILE *f
        	)
{
  
  LOAD ( _regs );
  LOAD ( _interruptions );
  
  return 0;
  
} /* end Z80_load_state */


int
Z80_save_state (
                FILE *f
        	)
{
  
  SAVE ( _regs );
  SAVE ( _interruptions );

  return 0;
  
} /* end Z80_save_state */
