ADC A, 0xaa              		;0xceaa        		#size:2
ADC A, A                 		;0x8f          		#size:1
ADC A, B                 		;0x88          		#size:1
ADC A, C                 		;0x89          		#size:1
ADC A, D                 		;0x8a          		#size:1
ADC A, E                 		;0x8b          		#size:1
ADC A, H                 		;0x8c          		#size:1
ADC A, L                 		;0x8d          		#size:1
ADC A, XH                		;0xdd8c        		#size:2
ADC A, XL                		;0xdd8d        		#size:2
ADC A, YH                		;0xfd8c        		#size:2
ADC A, YL                		;0xfd8d        		#size:2
ADC A, [HL]              		;0x8e          		#size:1
ADC A, [IX + 0x7F]       		;0xdd8e7f       		#size:3
ADC HL, BC               		;0xed4a        		#size:2
ADC HL, DE               		;0xed5a        		#size:2
ADC HL, HL               		;0xed6a        		#size:2
ADC HL, SP               		;0xed7a        		#size:2
ADD A, 0xaa              		;0xc6aa        		#size:2
ADD A, A                 		;0x87          		#size:1
ADD A, B                 		;0x80          		#size:1
ADD A, C                 		;0x81          		#size:1
ADD A, D                 		;0x82          		#size:1
ADD A, E                 		;0x83          		#size:1
ADD A, H                 		;0x84          		#size:1
ADD A, L                 		;0x85          		#size:1
ADD A, XH                		;0xdd84        		#size:2
ADD A, XL                		;0xdd85        		#size:2
ADD A, YH                		;0xfd84        		#size:2
ADD A, YL                		;0xfd85        		#size:2
ADD A, [HL]              		;0x86          		#size:1
ADD A, [IX + 0x7F]       		;0xdd867f       		#size:3
ADD HL, BC               		;0x09          		#size:1
ADD HL, DE               		;0x19          		#size:1
ADD HL, HL               		;0x29          		#size:1
ADD HL, SP               		;0x39          		#size:1
ADD IX, BC               		;0xdd09        		#size:2
ADD IX, DE               		;0xdd19        		#size:2
ADD IX, IX               		;0xdd29        		#size:2
ADD IX, SP               		;0xdd39        		#size:2
ADD IY, BC               		;0xfd09        		#size:2
ADD IY, DE               		;0xfd19        		#size:2
ADD IY, IY               		;0xfd29        		#size:2
ADD IY, SP               		;0xfd39        		#size:2
AND 0xaa                 		;0xe6aa        		#size:2
AND A                    		;0xa7          		#size:1
AND B                    		;0xa0          		#size:1
AND C                    		;0xa1          		#size:1
AND D                    		;0xa2          		#size:1
AND E                    		;0xa3          		#size:1
AND H                    		;0xa4          		#size:1
AND L                    		;0xa5          		#size:1
AND XH                   		;0xdda4        		#size:2
AND XL                   		;0xdda5        		#size:2
AND YH                   		;0xfda4        		#size:2
AND YL                   		;0xfda5        		#size:2
AND [HL]                 		;0xa6          		#size:1
AND [IX + 0x7F]       		  ;0xdda67f       		#size:3
BIT 0, A                 		;0xcb47        		#size:2
BIT 0, B                 		;0xcb40        		#size:2
BIT 0, C                 		;0xcb41        		#size:2
BIT 0, D                 		;0xcb42        		#size:2
BIT 0, E                 		;0xcb43        		#size:2
BIT 0, H                 		;0xcb44        		#size:2
BIT 0, L                 		;0xcb45        		#size:2
BIT 0, [HL]              		;0xcb46        		#size:2
BIT 0, [IX + 0x7F]       		;0xddcb7f41       		#size:4
BIT 1, A                 		;0xcb4f        		#size:2
BIT 1, B                 		;0xcb48        		#size:2
BIT 1, C                 		;0xcb49        		#size:2
BIT 1, D                 		;0xcb4a        		#size:2
BIT 1, E                 		;0xcb4b        		#size:2
BIT 1, H                 		;0xcb4c        		#size:2
BIT 1, L                 		;0xcb4d        		#size:2
BIT 1, [HL]              		;0xcb4e        		#size:2
BIT 1, [IX + 0x7F]       		;0xddcb7f48       		#size:4
BIT 2, A                 		;0xcb57        		#size:2
BIT 2, B                 		;0xcb50        		#size:2
BIT 2, C                 		;0xcb51        		#size:2
BIT 2, D                 		;0xcb52        		#size:2
BIT 2, E                 		;0xcb53        		#size:2
BIT 2, H                 		;0xcb54        		#size:2
BIT 2, L                 		;0xcb55        		#size:2
BIT 2, [HL]              		;0xcb56        		#size:2
BIT 2, [IX + 0x7F]       		;0xddcb7f55       		#size:4
BIT 3, A                 		;0xcb5f        		#size:2
BIT 3, B                 		;0xcb58        		#size:2
BIT 3, C                 		;0xcb59        		#size:2
BIT 3, D                 		;0xcb5a        		#size:2
BIT 3, E                 		;0xcb5b        		#size:2
BIT 3, H                 		;0xcb5c        		#size:2
BIT 3, L                 		;0xcb5d        		#size:2
BIT 3, [HL]              		;0xcb5e        		#size:2
BIT 3, [IX + 0x7F]       		;0xddcb7f5f       		#size:4
BIT 4, A                 		;0xcb67        		#size:2
BIT 4, B                 		;0xcb60        		#size:2
BIT 4, C                 		;0xcb61        		#size:2
BIT 4, D                 		;0xcb62        		#size:2
BIT 4, E                 		;0xcb63        		#size:2
BIT 4, H                 		;0xcb64        		#size:2
BIT 4, L                 		;0xcb65        		#size:2
BIT 4, [HL]              		;0xcb66        		#size:2
BIT 4, [IX + 0x7F]       		;0xddcb7f65       		#size:4
BIT 5, A                 		;0xcb6f        		#size:2
BIT 5, B                 		;0xcb68        		#size:2
BIT 5, C                 		;0xcb69        		#size:2
BIT 5, D                 		;0xcb6a        		#size:2
BIT 5, E                 		;0xcb6b        		#size:2
BIT 5, H                 		;0xcb6c        		#size:2
BIT 5, L                 		;0xcb6d        		#size:2
BIT 5, [HL]              		;0xcb6e        		#size:2
BIT 5, [IX + 0x7F]       		;0xddcb7f6b       		#size:4
BIT 6, A                 		;0xcb77        		#size:2
BIT 6, B                 		;0xcb70        		#size:2
BIT 6, C                 		;0xcb71        		#size:2
BIT 6, D                 		;0xcb72        		#size:2
BIT 6, E                 		;0xcb73        		#size:2
BIT 6, H                 		;0xcb74        		#size:2
BIT 6, L                 		;0xcb75        		#size:2
BIT 6, [HL]              		;0xcb76        		#size:2
BIT 6, [IX + 0x7F]       		;0xddcb7f70       		#size:4
BIT 7, A                 		;0xcb7f        		#size:2
BIT 7, B                 		;0xcb78        		#size:2
BIT 7, C                 		;0xcb79        		#size:2
BIT 7, D                 		;0xcb7a        		#size:2
BIT 7, E                 		;0xcb7b        		#size:2
BIT 7, H                 		;0xcb7c        		#size:2
BIT 7, L                 		;0xcb7d        		#size:2
BIT 7, [HL]              		;0xcb7e        		#size:2
BIT 7, [IX + 0x7F]       		;0xddcb7f7d       		#size:4
CALL 0xaaaa              		;0xcdaaaa      		#size:3
CALL C, 0xaaaa           		;0xdcaaaa      		#size:3
CALL M, 0xaaaa           		;0xfcaaaa      		#size:3
CALL NC, 0xaaaa          		;0xd4aaaa      		#size:3
CALL NZ, 0xaaaa          		;0xc4aaaa      		#size:3
CALL P, 0xaaaa           		;0xf4aaaa      		#size:3
CALL PE, 0xaaaa          		;0xecaaaa      		#size:3
CALL PO, 0xaaaa          		;0xe4aaaa      		#size:3
CALL Z, 0xaaaa           		;0xccaaaa      		#size:3
CCF                      		;0x3f          		#size:1
CP 0xaa                  		;0xfeaa        		#size:2
CP A                     		;0xbf          		#size:1
CP B                     		;0xb8          		#size:1
CP C                     		;0xb9          		#size:1
CP D                     		;0xba          		#size:1
CP E                     		;0xbb          		#size:1
CP H                     		;0xbc          		#size:1
CP L                     		;0xbd          		#size:1
CP XH                    		;0xddbc        		#size:2
CP XL                    		;0xddbd        		#size:2
CP YH                    		;0xfdbc        		#size:2
CP YL                    		;0xfdbd        		#size:2
CP [HL]                  		;0xbe          		#size:1
CP [IX + 0x7F]       		;0xddbe7f       		#size:3
CPD                      		;0xeda9        		#size:2
CPDR                     		;0xedb9        		#size:2
CPI                      		;0xeda1        		#size:2
CPIR                     		;0xedb1        		#size:2
CPL                      		;0x2f          		#size:1
DAA                      		;0x27          		#size:1
DEC A                    		;0x3d          		#size:1
DEC B                    		;0x05          		#size:1
DEC BC                   		;0x0b          		#size:1
DEC C                    		;0x0d          		#size:1
DEC D                    		;0x15          		#size:1
DEC DE                   		;0x1b          		#size:1
DEC E                    		;0x1d          		#size:1
DEC H                    		;0x25          		#size:1
DEC HL                   		;0x2b          		#size:1
DEC IX                   		;0xdd2b        		#size:2
DEC IY                   		;0xfd2b        		#size:2
DEC L                    		;0x2d          		#size:1
DEC SP                   		;0x3b          		#size:1
DEC XH                   		;0xdd25        		#size:2
DEC XL                   		;0xdd2d        		#size:2
DEC YH                   		;0xfd25        		#size:2
DEC YL                   		;0xfd2d        		#size:2
DEC [HL]                 		;0x35          		#size:1
DEC [IX + 0x7F]       		;0xdd357f       		#size:3
DI                       		;0xf3          		#size:1
DJNZ 0                		;0x10aa        		#size:2
EI                       		;0xfb          		#size:1
EX AF, AF'               		;0x08          		#size:1
EX DE, HL                		;0xeb          		#size:1
EX [SP], HL              		;0xe3          		#size:1
EX [SP], IX              		;0xdde3        		#size:2
EX [SP], IY              		;0xfde3        		#size:2
EXX                      		;0xd9          		#size:1
HALT                     		;0x76          		#size:1
IM 0                     		;0xed66        		#size:2
IM 1                     		;0xed76        		#size:2
IM 2                     		;0xed5e        		#size:2
IN A, [0xaa]             		;0xdbaa        		#size:2
IN A, [C]                		;0xed78        		#size:2
IN B, [C]                		;0xed40        		#size:2
IN C, [C]                		;0xed48        		#size:2
IN D, [C]                		;0xed50        		#size:2
IN E, [C]                		;0xed58        		#size:2
IN F, [C]                		;0xed70        		#size:2
IN H, [C]                		;0xed60        		#size:2
IN L, [C]                		;0xed68        		#size:2
IN [C], [C]              		;0xed70        		#size:2
INC A                    		;0x3c          		#size:1
INC B                    		;0x04          		#size:1
INC BC                   		;0x03          		#size:1
INC C                    		;0x0c          		#size:1
INC D                    		;0x14          		#size:1
INC DE                   		;0x13          		#size:1
INC E                    		;0x1c          		#size:1
INC H                    		;0x24          		#size:1
INC HL                   		;0x23          		#size:1
INC IX                   		;0xdd23        		#size:2
INC IY                   		;0xfd23        		#size:2
INC L                    		;0x2c          		#size:1
INC SP                   		;0x33          		#size:1
INC XH                   		;0xdd24        		#size:2
INC XL                   		;0xdd2c        		#size:2
INC YH                   		;0xfd24        		#size:2
INC YL                   		;0xfd2c        		#size:2
INC [HL]                 		;0x34          		#size:1
INC [IX + 0x7F]       		;0xdd347f       		#size:3
IND                      		;0xedaa        		#size:2
INDR                     		;0xedba        		#size:2
INI                      		;0xeda2        		#size:2
INIR                     		;0xedb2        		#size:2
JP 0xaaaa                		;0xc3aaaa      		#size:3
JP C, 0xaaaa             		;0xdaaaaa      		#size:3
JP M, 0xaaaa             		;0xfaaaaa      		#size:3
JP NC, 0xaaaa            		;0xd2aaaa      		#size:3
JP NZ, 0xaaaa            		;0xc2aaaa      		#size:3
JP P, 0xaaaa             		;0xf2aaaa      		#size:3
JP PE, 0xaaaa            		;0xeaaaaa      		#size:3
JP PO, 0xaaaa            		;0xe2aaaa      		#size:3
JP Z, 0xaaaa             		;0xcaaaaa      		#size:3
JP [HL]                  		;0xe9          		#size:1
JP [IX]                  		;0xdde9        		#size:2
JP [IY]                  		;0xfde9        		#size:2
JR 100                  		;0x18aa        		#size:2
JR C, 100               		;0x38aa        		#size:2
JR NC, 100              		;0x30aa        		#size:2
JR NZ, 100              		;0x20aa        		#size:2
JR Z, 100               		;0x28aa        		#size:2
LD A, 0xaa               		;0x3eaa        		#size:2
LD A, A                  		;0x7f          		#size:1
LD A, B                  		;0x78          		#size:1
LD A, C                  		;0x79          		#size:1
LD A, D                  		;0x7a          		#size:1
LD A, E                  		;0x7b          		#size:1
LD A, H                  		;0x7c          		#size:1
LD A, I                  		;0xed57        		#size:2
LD A, L                  		;0x7d          		#size:1
LD A, R                  		;0xed5f        		#size:2
LD A, RES 0, [IX + 0x7F]       		;0xddcb7f87       		#size:4
LD A, RES 1, [IX + 0x7F]       		;0xddcb7f8f       		#size:4
LD A, RES 2, [IX + 0x7F]       		;0xddcb7f97       		#size:4
LD A, RES 3, [IX + 0x7F]       		;0xddcb7f9f       		#size:4
LD A, RES 4, [IX + 0x7F]       		;0xddcb7fa7       		#size:4
LD A, RES 5, [IX + 0x7F]       		;0xddcb7faf       		#size:4
LD A, RES 6, [IX + 0x7F]       		;0xddcb7fb7       		#size:4
LD A, RES 7, [IX + 0x7F]       		;0xddcb7fbf       		#size:4
LD A, RL [IX + 0x7F]       		;0xddcb7f17       		#size:4
LD A, RLC [IX + 0x7F]       		;0xddcb7f07       		#size:4
LD A, RR [IX + 0x7F]       		;0xddcb7f1f       		#size:4
LD A, RRC [IX + 0x7F]       		;0xddcb7f0f       		#size:4
LD A, SET 0, [IX + 0x7F]       		;0xddcb7fc7       		#size:4
LD A, SET 1, [IX + 0x7F]       		;0xddcb7fcf       		#size:4
LD A, SET 2, [IX + 0x7F]       		;0xddcb7fd7       		#size:4
LD A, SET 3, [IX + 0x7F]       		;0xddcb7fdf       		#size:4
LD A, SET 4, [IX + 0x7F]       		;0xddcb7fe7       		#size:4
LD A, SET 5, [IX + 0x7F]       		;0xddcb7fef       		#size:4
LD A, SET 6, [IX + 0x7F]       		;0xddcb7ff7       		#size:4
LD A, SET 7, [IX + 0x7F]       		;0xddcb7fff       		#size:4
LD A, SLA [IX + 0x7F]       		;0xddcb7f27       		#size:4
LD A, SLL [IX + 0x7F]       		;0xddcb7f37       		#size:4
LD A, SRA [IX + 0x7F]       		;0xddcb7f2f       		#size:4
LD A, SRL [IX + 0x7F]       		;0xddcb7f3f       		#size:4
LD A, XH                 		;0xdd7c        		#size:2
LD A, XL                 		;0xdd7d        		#size:2
LD A, YH                 		;0xfd7c        		#size:2
LD A, YL                 		;0xfd7d        		#size:2
LD A, [0xaaaa]           		;0x3aaaaa      		#size:3
LD A, [BC]               		;0x0a          		#size:1
LD A, [DE]               		;0x1a          		#size:1
LD A, [HL]               		;0x7e          		#size:1
LD A, [IX + 0x7F]       		;0xdd7e7f       		#size:3
LD B, 0xaa               		;0x06aa        		#size:2
LD B, A                  		;0x47          		#size:1
LD B, B                  		;0x40          		#size:1
LD B, C                  		;0x41          		#size:1
LD B, D                  		;0x42          		#size:1
LD B, E                  		;0x43          		#size:1
LD B, H                  		;0x44          		#size:1
LD B, L                  		;0x45          		#size:1
LD B, RES 0, [IX + 0x7F]       		;0xddcb7f80       		#size:4
LD B, RES 1, [IX + 0x7F]       		;0xddcb7f88       		#size:4
LD B, RES 2, [IX + 0x7F]       		;0xddcb7f90       		#size:4
LD B, RES 3, [IX + 0x7F]       		;0xddcb7f98       		#size:4
LD B, RES 4, [IX + 0x7F]       		;0xddcb7fa0       		#size:4
LD B, RES 5, [IX + 0x7F]       		;0xddcb7fa8       		#size:4
LD B, RES 6, [IX + 0x7F]       		;0xddcb7fb0       		#size:4
LD B, RES 7, [IX + 0x7F]       		;0xddcb7fb8       		#size:4
LD B, RL [IX + 0x7F]       		;0xddcb7f10       		#size:4
LD B, RLC [IX + 0x7F]       		;0xddcb7f00       		#size:4
LD B, RR [IX + 0x7F]       		;0xddcb7f18       		#size:4
LD B, RRC [IX + 0x7F]       		;0xddcb7f08       		#size:4
LD B, SET 0, [IX + 0x7F]       		;0xddcb7fc0       		#size:4
LD B, SET 1, [IX + 0x7F]       		;0xddcb7fc8       		#size:4
LD B, SET 2, [IX + 0x7F]       		;0xddcb7fd0       		#size:4
LD B, SET 3, [IX + 0x7F]       		;0xddcb7fd8       		#size:4
LD B, SET 4, [IX + 0x7F]       		;0xddcb7fe0       		#size:4
LD B, SET 5, [IX + 0x7F]       		;0xddcb7fe8       		#size:4
LD B, SET 6, [IX + 0x7F]       		;0xddcb7ff0       		#size:4
LD B, SET 7, [IX + 0x7F]       		;0xddcb7ff8       		#size:4
LD B, SLA [IX + 0x7F]       		;0xddcb7f20       		#size:4
LD B, SLL [IX + 0x7F]       		;0xddcb7f30       		#size:4
LD B, SRA [IX + 0x7F]       		;0xddcb7f28       		#size:4
LD B, SRL [IX + 0x7F]       		;0xddcb7f38       		#size:4
LD B, XH                 		;0xdd44        		#size:2
LD B, XL                 		;0xdd45        		#size:2
LD B, YH                 		;0xfd44        		#size:2
LD B, YL                 		;0xfd45        		#size:2
LD B, [HL]               		;0x46          		#size:1
LD B, [IX + 0x7F]       		;0xdd467f       		#size:3
LD BC, 0xaaaa            		;0x01aaaa      		#size:3
LD BC, [0xaaaa]          		;0xed4baaaa    		#size:4
LD C, 0xaa               		;0x0eaa        		#size:2
LD C, A                  		;0x4f          		#size:1
LD C, B                  		;0x48          		#size:1
LD C, C                  		;0x49          		#size:1
LD C, D                  		;0x4a          		#size:1
LD C, E                  		;0x4b          		#size:1
LD C, H                  		;0x4c          		#size:1
LD C, L                  		;0x4d          		#size:1
LD C, RES 0, [IX + 0x7F]       		;0xddcb7f81       		#size:4
LD C, RES 1, [IX + 0x7F]       		;0xddcb7f89       		#size:4
LD C, RES 2, [IX + 0x7F]       		;0xddcb7f91       		#size:4
LD C, RES 3, [IX + 0x7F]       		;0xddcb7f99       		#size:4
LD C, RES 4, [IX + 0x7F]       		;0xddcb7fa1       		#size:4
LD C, RES 5, [IX + 0x7F]       		;0xddcb7fa9       		#size:4
LD C, RES 6, [IX + 0x7F]       		;0xddcb7fb1       		#size:4
LD C, RES 7, [IX + 0x7F]       		;0xddcb7fb9       		#size:4
LD C, RL [IX + 0x7F]       		;0xddcb7f11       		#size:4
LD C, RLC [IX + 0x7F]       		;0xddcb7f01       		#size:4
LD C, RR [IX + 0x7F]       		;0xddcb7f19       		#size:4
LD C, RRC [IX + 0x7F]       		;0xddcb7f09       		#size:4
LD C, SET 0, [IX + 0x7F]       		;0xddcb7fc1       		#size:4
LD C, SET 1, [IX + 0x7F]       		;0xddcb7fc9       		#size:4
LD C, SET 2, [IX + 0x7F]       		;0xddcb7fd1       		#size:4
LD C, SET 3, [IX + 0x7F]       		;0xddcb7fd9       		#size:4
LD C, SET 4, [IX + 0x7F]       		;0xddcb7fe1       		#size:4
LD C, SET 5, [IX + 0x7F]       		;0xddcb7fe9       		#size:4
LD C, SET 6, [IX + 0x7F]       		;0xddcb7ff1       		#size:4
LD C, SET 7, [IX + 0x7F]       		;0xddcb7ff9       		#size:4
LD C, SLA [IX + 0x7F]       		;0xddcb7f21       		#size:4
LD C, SLL [IX + 0x7F]       		;0xddcb7f31       		#size:4
LD C, SRA [IX + 0x7F]       		;0xddcb7f29       		#size:4
LD C, SRL [IX + 0x7F]       		;0xddcb7f39       		#size:4
LD C, XH                 		;0xdd4c        		#size:2
LD C, XL                 		;0xdd4d        		#size:2
LD C, YH                 		;0xfd4c        		#size:2
LD C, YL                 		;0xfd4d        		#size:2
LD C, [HL]               		;0x4e          		#size:1
LD C, [IX + 0x7F]       		;0xdd4e7f       		#size:3
LD D, 0xaa               		;0x16aa        		#size:2
LD D, A                  		;0x57          		#size:1
LD D, B                  		;0x50          		#size:1
LD D, C                  		;0x51          		#size:1
LD D, D                  		;0x52          		#size:1
LD D, E                  		;0x53          		#size:1
LD D, H                  		;0x54          		#size:1
LD D, L                  		;0x55          		#size:1
LD D, RES 0, [IX + 0x7F]       		;0xddcb7f82       		#size:4
LD D, RES 1, [IX + 0x7F]       		;0xddcb7f8a       		#size:4
LD D, RES 2, [IX + 0x7F]       		;0xddcb7f92       		#size:4
LD D, RES 3, [IX + 0x7F]       		;0xddcb7f9a       		#size:4
LD D, RES 4, [IX + 0x7F]       		;0xddcb7fa2       		#size:4
LD D, RES 5, [IX + 0x7F]       		;0xddcb7faa       		#size:4
LD D, RES 6, [IX + 0x7F]       		;0xddcb7fb2       		#size:4
LD D, RES 7, [IX + 0x7F]       		;0xddcb7fba       		#size:4
LD D, RL [IX + 0x7F]       		;0xddcb7f12       		#size:4
LD D, RLC [IX + 0x7F]       		;0xddcb7f02       		#size:4
LD D, RR [IX + 0x7F]       		;0xddcb7f1a       		#size:4
LD D, RRC [IX + 0x7F]       		;0xddcb7f0a       		#size:4
LD D, SET 0, [IX + 0x7F]       		;0xddcb7fc2       		#size:4
LD D, SET 1, [IX + 0x7F]       		;0xddcb7fca       		#size:4
LD D, SET 2, [IX + 0x7F]       		;0xddcb7fd2       		#size:4
LD D, SET 3, [IX + 0x7F]       		;0xddcb7fda       		#size:4
LD D, SET 4, [IX + 0x7F]       		;0xddcb7fe2       		#size:4
LD D, SET 5, [IX + 0x7F]       		;0xddcb7fea       		#size:4
LD D, SET 6, [IX + 0x7F]       		;0xddcb7ff2       		#size:4
LD D, SET 7, [IX + 0x7F]       		;0xddcb7ffa       		#size:4
LD D, SLA [IX + 0x7F]       		;0xddcb7f22       		#size:4
LD D, SLL [IX + 0x7F]       		;0xddcb7f32       		#size:4
LD D, SRA [IX + 0x7F]       		;0xddcb7f2a       		#size:4
LD D, SRL [IX + 0x7F]       		;0xddcb7f3a       		#size:4
LD D, XH                 		;0xdd54        		#size:2
LD D, XL                 		;0xdd55        		#size:2
LD D, YH                 		;0xfd54        		#size:2
LD D, YL                 		;0xfd55        		#size:2
LD D, [HL]               		;0x56          		#size:1
LD D, [IX + 0x7F]       		;0xdd567f       		#size:3
LD DE, 0xaaaa            		;0x11aaaa      		#size:3
LD DE, [0xaaaa]          		;0xed5baaaa    		#size:4
LD E, 0xaa               		;0x1eaa        		#size:2
LD E, A                  		;0x5f          		#size:1
LD E, B                  		;0x58          		#size:1
LD E, C                  		;0x59          		#size:1
LD E, D                  		;0x5a          		#size:1
LD E, E                  		;0x5b          		#size:1
LD E, H                  		;0x5c          		#size:1
LD E, L                  		;0x5d          		#size:1
LD E, RES 0, [IX + 0x7F]       		;0xddcb7f83       		#size:4
LD E, RES 1, [IX + 0x7F]       		;0xddcb7f8b       		#size:4
LD E, RES 2, [IX + 0x7F]       		;0xddcb7f93       		#size:4
LD E, RES 3, [IX + 0x7F]       		;0xddcb7f9b       		#size:4
LD E, RES 4, [IX + 0x7F]       		;0xddcb7fa3       		#size:4
LD E, RES 5, [IX + 0x7F]       		;0xddcb7fab       		#size:4
LD E, RES 6, [IX + 0x7F]       		;0xddcb7fb3       		#size:4
LD E, RES 7, [IX + 0x7F]       		;0xddcb7fbb       		#size:4
LD E, RL [IX + 0x7F]       		;0xddcb7f13       		#size:4
LD E, RLC [IX + 0x7F]       		;0xddcb7f03       		#size:4
LD E, RR [IX + 0x7F]       		;0xddcb7f1b       		#size:4
LD E, RRC [IX + 0x7F]       		;0xddcb7f0b       		#size:4
LD E, SET 0, [IX + 0x7F]       		;0xddcb7fc3       		#size:4
LD E, SET 1, [IX + 0x7F]       		;0xddcb7fcb       		#size:4
LD E, SET 2, [IX + 0x7F]       		;0xddcb7fd3       		#size:4
LD E, SET 3, [IX + 0x7F]       		;0xddcb7fdb       		#size:4
LD E, SET 4, [IX + 0x7F]       		;0xddcb7fe3       		#size:4
LD E, SET 5, [IX + 0x7F]       		;0xddcb7feb       		#size:4
LD E, SET 6, [IX + 0x7F]       		;0xddcb7ff3       		#size:4
LD E, SET 7, [IX + 0x7F]       		;0xddcb7ffb       		#size:4
LD E, SLA [IX + 0x7F]       		;0xddcb7f23       		#size:4
LD E, SLL [IX + 0x7F]       		;0xddcb7f33       		#size:4
LD E, SRA [IX + 0x7F]       		;0xddcb7f2b       		#size:4
LD E, SRL [IX + 0x7F]       		;0xddcb7f3b       		#size:4
LD E, XH                 		;0xdd5c        		#size:2
LD E, XL                 		;0xdd5d        		#size:2
LD E, YH                 		;0xfd5c        		#size:2
LD E, YL                 		;0xfd5d        		#size:2
LD E, [HL]               		;0x5e          		#size:1
LD E, [IX + 0x7F]       		;0xdd5e7f       		#size:3
LD H, 0xaa               		;0x26aa        		#size:2
LD H, A                  		;0x67          		#size:1
LD H, B                  		;0x60          		#size:1
LD H, C                  		;0x61          		#size:1
LD H, D                  		;0x62          		#size:1
LD H, E                  		;0x63          		#size:1
LD H, H                  		;0x64          		#size:1
LD H, L                  		;0x65          		#size:1
LD H, RES 0, [IX + 0x7F]       		;0xddcb7f84       		#size:4
LD H, RES 1, [IX + 0x7F]       		;0xddcb7f8c       		#size:4
LD H, RES 2, [IX + 0x7F]       		;0xddcb7f94       		#size:4
LD H, RES 3, [IX + 0x7F]       		;0xddcb7f9c       		#size:4
LD H, RES 4, [IX + 0x7F]       		;0xddcb7fa4       		#size:4
LD H, RES 5, [IX + 0x7F]       		;0xddcb7fac       		#size:4
LD H, RES 6, [IX + 0x7F]       		;0xddcb7fb4       		#size:4
LD H, RES 7, [IX + 0x7F]       		;0xddcb7fbc       		#size:4
LD H, RL [IX + 0x7F]       		;0xddcb7f14       		#size:4
LD H, RLC [IX + 0x7F]       		;0xddcb7f04       		#size:4
LD H, RR [IX + 0x7F]       		;0xddcb7f1c       		#size:4
LD H, RRC [IX + 0x7F]       		;0xddcb7f0c       		#size:4
LD H, SET 0, [IX + 0x7F]       		;0xddcb7fc4       		#size:4
LD H, SET 1, [IX + 0x7F]       		;0xddcb7fcc       		#size:4
LD H, SET 2, [IX + 0x7F]       		;0xddcb7fd4       		#size:4
LD H, SET 3, [IX + 0x7F]       		;0xddcb7fdc       		#size:4
LD H, SET 4, [IX + 0x7F]       		;0xddcb7fe4       		#size:4
LD H, SET 5, [IX + 0x7F]       		;0xddcb7fec       		#size:4
LD H, SET 6, [IX + 0x7F]       		;0xddcb7ff4       		#size:4
LD H, SET 7, [IX + 0x7F]       		;0xddcb7ffc       		#size:4
LD H, SLA [IX + 0x7F]       		;0xddcb7f24       		#size:4
LD H, SLL [IX + 0x7F]       		;0xddcb7f34       		#size:4
LD H, SRA [IX + 0x7F]       		;0xddcb7f2c       		#size:4
LD H, SRL [IX + 0x7F]       		;0xddcb7f3c       		#size:4
LD H, [HL]               		;0x66          		#size:1
LD H, [IX + 0x7F]       		;0xdd667f       		#size:3
LD HL, 0xaaaa            		;0x21aaaa      		#size:3
LD HL, [0xaaaa]          		;0xed6baaaa    		#size:4
LD I, A                  		;0xed47        		#size:2
LD IX, 0xaaaa            		;0xdd21aaaa    		#size:4
LD IX, [0xaaaa]          		;0xdd2aaaaa    		#size:4
LD IY, 0xaaaa            		;0xfd21aaaa    		#size:4
LD IY, [0xaaaa]          		;0xfd2aaaaa    		#size:4
LD L, 0xaa               		;0x2eaa        		#size:2
LD L, A                  		;0x6f          		#size:1
LD L, B                  		;0x68          		#size:1
LD L, C                  		;0x69          		#size:1
LD L, D                  		;0x6a          		#size:1
LD L, E                  		;0x6b          		#size:1
LD L, H                  		;0x6c          		#size:1
LD L, L                  		;0x6d          		#size:1
LD L, RES 0, [IX + 0x7F]       		;0xddcb7f85       		#size:4
LD L, RES 1, [IX + 0x7F]       		;0xddcb7f8d       		#size:4
LD L, RES 2, [IX + 0x7F]       		;0xddcb7f95       		#size:4
LD L, RES 3, [IX + 0x7F]       		;0xddcb7f9d       		#size:4
LD L, RES 4, [IX + 0x7F]       		;0xddcb7fa5       		#size:4
LD L, RES 5, [IX + 0x7F]       		;0xddcb7fad       		#size:4
LD L, RES 6, [IX + 0x7F]       		;0xddcb7fb5       		#size:4
LD L, RES 7, [IX + 0x7F]       		;0xddcb7fbd       		#size:4
LD L, RL [IX + 0x7F]       		;0xddcb7f15       		#size:4
LD L, RLC [IX + 0x7F]       		;0xddcb7f05       		#size:4
LD L, RR [IX + 0x7F]       		;0xddcb7f1d       		#size:4
LD L, RRC [IX + 0x7F]       		;0xddcb7f0d       		#size:4
LD L, SET 0, [IX + 0x7F]       		;0xddcb7fc5       		#size:4
LD L, SET 1, [IX + 0x7F]       		;0xddcb7fcd       		#size:4
LD L, SET 2, [IX + 0x7F]       		;0xddcb7fd5       		#size:4
LD L, SET 3, [IX + 0x7F]       		;0xddcb7fdd       		#size:4
LD L, SET 4, [IX + 0x7F]       		;0xddcb7fe5       		#size:4
LD L, SET 5, [IX + 0x7F]       		;0xddcb7fed       		#size:4
LD L, SET 6, [IX + 0x7F]       		;0xddcb7ff5       		#size:4
LD L, SET 7, [IX + 0x7F]       		;0xddcb7ffd       		#size:4
LD L, SLA [IX + 0x7F]       		;0xddcb7f25       		#size:4
LD L, SLL [IX + 0x7F]       		;0xddcb7f35       		#size:4
LD L, SRA [IX + 0x7F]       		;0xddcb7f2d       		#size:4
LD L, SRL [IX + 0x7F]       		;0xddcb7f3d       		#size:4
LD L, [HL]               		;0x6e          		#size:1
LD L, [IX + 0x7F]       		;0xdd6e7f       		#size:3
LD R, A                  		;0xed4f        		#size:2
LD SP, 0xaaaa            		;0x31aaaa      		#size:3
LD SP, HL                		;0xf9          		#size:1
LD SP, IX                		;0xddf9        		#size:2
LD SP, IY                		;0xfdf9        		#size:2
LD SP, [0xaaaa]          		;0xed7baaaa    		#size:4
LD XH, 0xaa              		;0xdd26aa      		#size:3
LD XH, A                 		;0xdd67        		#size:2
LD XH, B                 		;0xdd60        		#size:2
LD XH, C                 		;0xdd61        		#size:2
LD XH, D                 		;0xdd62        		#size:2
LD XH, E                 		;0xdd63        		#size:2
LD XH, XH                		;0xdd64        		#size:2
LD XH, XL                		;0xdd65        		#size:2
LD XL, 0xaa              		;0xdd2eaa      		#size:3
LD XL, A                 		;0xdd6f        		#size:2
LD XL, B                 		;0xdd68        		#size:2
LD XL, C                 		;0xdd69        		#size:2
LD XL, D                 		;0xdd6a        		#size:2
LD XL, E                 		;0xdd6b        		#size:2
LD XL, XH                		;0xdd6c        		#size:2
LD XL, XL                		;0xdd6d        		#size:2
LD YH, 0xaa              		;0xfd26aa      		#size:3
LD YH, A                 		;0xfd67        		#size:2
LD YH, B                 		;0xfd60        		#size:2
LD YH, C                 		;0xfd61        		#size:2
LD YH, D                 		;0xfd62        		#size:2
LD YH, E                 		;0xfd63        		#size:2
LD YH, YH                		;0xfd64        		#size:2
LD YH, YL                		;0xfd65        		#size:2
LD YL, 0xaa              		;0xfd2eaa      		#size:3
LD YL, A                 		;0xfd6f        		#size:2
LD YL, B                 		;0xfd68        		#size:2
LD YL, C                 		;0xfd69        		#size:2
LD YL, D                 		;0xfd6a        		#size:2
LD YL, E                 		;0xfd6b        		#size:2
LD YL, YH                		;0xfd6c        		#size:2
LD YL, YL                		;0xfd6d        		#size:2
LD [0xaaaa], A           		;0x32aaaa      		#size:3
LD [0xaaaa], BC          		;0xed43aaaa    		#size:4
LD [0xaaaa], DE          		;0xed53aaaa    		#size:4
LD [0xaaaa], HL          		;0xed63aaaa    		#size:4
LD [0xaaaa], IX          		;0xdd22aaaa    		#size:4
LD [0xaaaa], IY          		;0xfd22aaaa    		#size:4
LD [0xaaaa], SP          		;0xed73aaaa    		#size:4
LD [BC], A               		;0x02          		#size:1
LD [DE], A               		;0x12          		#size:1
LD [HL], 0xaa            		;0x36aa        		#size:2
LD [HL], A               		;0x77          		#size:1
LD [HL], B               		;0x70          		#size:1
LD [HL], C               		;0x71          		#size:1
LD [HL], D               		;0x72          		#size:1
LD [HL], E               		;0x73          		#size:1
LD [HL], H               		;0x74          		#size:1
LD [HL], L               		;0x75          		#size:1
LD [IX + 0x7F], 0xaa       		;0xdd367f       		#size:3
LD [IX + 0x7F], A       		;0xdd777f       		#size:3
LD [IX + 0x7F], B       		;0xdd707f       		#size:3
LD [IX + 0x7F], C       		;0xdd717f       		#size:3
LD [IX + 0x7F], D       		;0xdd727f       		#size:3
LD [IX + 0x7F], E       		;0xdd737f       		#size:3
LD [IX + 0x7F], H       		;0xdd747f       		#size:3
LD [IX + 0x7F], L       		;0xdd757f       		#size:3
LDD                      		;0xeda8        		#size:2
LDDR                     		;0xedb8        		#size:2
LDI                      		;0xeda0        		#size:2
LDIR                     		;0xedb0        		#size:2
NEG                      		;0xed7c        		#size:2
NOP                      		;00            		#size:1
OR 0xaa                  		;0xf6aa        		#size:2
OR A                     		;0xb7          		#size:1
OR B                     		;0xb0          		#size:1
OR C                     		;0xb1          		#size:1
OR D                     		;0xb2          		#size:1
OR E                     		;0xb3          		#size:1
OR H                     		;0xb4          		#size:1
OR L                     		;0xb5          		#size:1
OR XH                    		;0xddb4        		#size:2
OR XL                    		;0xddb5        		#size:2
OR YH                    		;0xfdb4        		#size:2
OR YL                    		;0xfdb5        		#size:2
OR [HL]                  		;0xb6          		#size:1
OR [IX + 0x7F]       		;0xfdb67f       		#size:3
OTDR                     		;0xedbb        		#size:2
OTIR                     		;0xedb3        		#size:2
OUT [0xaa], A            		;0xd3aa        		#size:2
OUT [C], 0               		;0xed71        		#size:2
OUT [C], A               		;0xed79        		#size:2
OUT [C], B               		;0xed41        		#size:2
OUT [C], C               		;0xed49        		#size:2
OUT [C], D               		;0xed51        		#size:2
OUT [C], E               		;0xed59        		#size:2
OUT [C], H               		;0xed61        		#size:2
OUT [C], L               		;0xed69        		#size:2
OUTD                     		;0xedab        		#size:2
OUTI                     		;0xeda3        		#size:2
POP AF                   		;0xf1          		#size:1
POP BC                   		;0xc1          		#size:1
POP DE                   		;0xd1          		#size:1
POP HL                   		;0xe1          		#size:1
POP IX                   		;0xdde1        		#size:2
POP IY                   		;0xfde1        		#size:2
PUSH AF                  		;0xf5          		#size:1
PUSH BC                  		;0xc5          		#size:1
PUSH DE                  		;0xd5          		#size:1
PUSH HL                  		;0xe5          		#size:1
PUSH IX                  		;0xdde5        		#size:2
PUSH IY                  		;0xfde5        		#size:2
RES 0, A                 		;0xcb87        		#size:2
RES 0, B                 		;0xcb80        		#size:2
RES 0, C                 		;0xcb81        		#size:2
RES 0, D                 		;0xcb82        		#size:2
RES 0, E                 		;0xcb83        		#size:2
RES 0, H                 		;0xcb84        		#size:2
RES 0, L                 		;0xcb85        		#size:2
RES 0, [HL]              		;0xcb86        		#size:2
RES 0, [IX + 0x7F]       		;0xddcb7f86       		#size:4
RES 1, A                 		;0xcb8f        		#size:2
RES 1, B                 		;0xcb88        		#size:2
RES 1, C                 		;0xcb89        		#size:2
RES 1, D                 		;0xcb8a        		#size:2
RES 1, E                 		;0xcb8b        		#size:2
RES 1, H                 		;0xcb8c        		#size:2
RES 1, L                 		;0xcb8d        		#size:2
RES 1, [HL]              		;0xcb8e        		#size:2
RES 1, [IX + 0x7F]       		;0xddcb7f8e       		#size:4
RES 2, A                 		;0xcb97        		#size:2
RES 2, B                 		;0xcb90        		#size:2
RES 2, C                 		;0xcb91        		#size:2
RES 2, D                 		;0xcb92        		#size:2
RES 2, E                 		;0xcb93        		#size:2
RES 2, H                 		;0xcb94        		#size:2
RES 2, L                 		;0xcb95        		#size:2
RES 2, [HL]              		;0xcb96        		#size:2
RES 2, [IX + 0x7F]       		;0xddcb7f96       		#size:4
RES 3, A                 		;0xcb9f        		#size:2
RES 3, B                 		;0xcb98        		#size:2
RES 3, C                 		;0xcb99        		#size:2
RES 3, D                 		;0xcb9a        		#size:2
RES 3, E                 		;0xcb9b        		#size:2
RES 3, H                 		;0xcb9c        		#size:2
RES 3, L                 		;0xcb9d        		#size:2
RES 3, [HL]              		;0xcb9e        		#size:2
RES 3, [IX + 0x7F]       		;0xddcb7f9e       		#size:4
RES 4, A                 		;0xcba7        		#size:2
RES 4, B                 		;0xcba0        		#size:2
RES 4, C                 		;0xcba1        		#size:2
RES 4, D                 		;0xcba2        		#size:2
RES 4, E                 		;0xcba3        		#size:2
RES 4, H                 		;0xcba4        		#size:2
RES 4, L                 		;0xcba5        		#size:2
RES 4, [HL]              		;0xcba6        		#size:2
RES 4, [IX + 0x7F]       		;0xddcb7fa6       		#size:4
RES 5, A                 		;0xcbaf        		#size:2
RES 5, B                 		;0xcba8        		#size:2
RES 5, C                 		;0xcba9        		#size:2
RES 5, D                 		;0xcbaa        		#size:2
RES 5, E                 		;0xcbab        		#size:2
RES 5, H                 		;0xcbac        		#size:2
RES 5, L                 		;0xcbad        		#size:2
RES 5, [HL]              		;0xcbae        		#size:2
RES 5, [IX + 0x7F]       		;0xddcb7fae       		#size:4
RES 6, A                 		;0xcbb7        		#size:2
RES 6, B                 		;0xcbb0        		#size:2
RES 6, C                 		;0xcbb1        		#size:2
RES 6, D                 		;0xcbb2        		#size:2
RES 6, E                 		;0xcbb3        		#size:2
RES 6, H                 		;0xcbb4        		#size:2
RES 6, L                 		;0xcbb5        		#size:2
RES 6, [HL]              		;0xcbb6        		#size:2
RES 6, [IX + 0x7F]       		;0xddcb7fb6       		#size:4
RES 7, A                 		;0xcbbf        		#size:2
RES 7, B                 		;0xcbb8        		#size:2
RES 7, C                 		;0xcbb9        		#size:2
RES 7, D                 		;0xcbba        		#size:2
RES 7, E                 		;0xcbbb        		#size:2
RES 7, H                 		;0xcbbc        		#size:2
RES 7, L                 		;0xcbbd        		#size:2
RES 7, [HL]              		;0xcbbe        		#size:2
RES 7, [IX + 0x7F]       		;0xddcb7fbe       		#size:4
RET                      		;0xc9          		#size:1
RET C                    		;0xd8          		#size:1
RET M                    		;0xf8          		#size:1
RET NC                   		;0xd0          		#size:1
RET NZ                   		;0xc0          		#size:1
RET P                    		;0xf0          		#size:1
RET PE                   		;0xe8          		#size:1
RET PO                   		;0xe0          		#size:1
RET Z                    		;0xc8          		#size:1
RETI                     		;0xed4d        		#size:2
RETN                     		;0xed45        		#size:2
RL A                     		;0xcb17        		#size:2
RL B                     		;0xcb10        		#size:2
RL C                     		;0xcb11        		#size:2
RL D                     		;0xcb12        		#size:2
RL E                     		;0xcb13        		#size:2
RL H                     		;0xcb14        		#size:2
RL L                     		;0xcb15        		#size:2
RL [HL]                  		;0xcb16        		#size:2
RL [IX + 0x7F]       		;0xddcb7f16       		#size:4
RLA                      		;0x17          		#size:1
RLC A                    		;0xcb07        		#size:2
RLC B                    		;0xcb00        		#size:2
RLC C                    		;0xcb01        		#size:2
RLC D                    		;0xcb02        		#size:2
RLC E                    		;0xcb03        		#size:2
RLC H                    		;0xcb04        		#size:2
RLC L                    		;0xcb05        		#size:2
RLC [HL]                 		;0xcb06        		#size:2
RLC [IX + 0x7F]       		;0xddcb7f06       		#size:4
RLCA                     		;0x07          		#size:1
RLD                      		;0xed6f        		#size:2
RR A                     		;0xcb1f        		#size:2
RR B                     		;0xcb18        		#size:2
RR C                     		;0xcb19        		#size:2
RR D                     		;0xcb1a        		#size:2
RR E                     		;0xcb1b        		#size:2
RR H                     		;0xcb1c        		#size:2
RR L                     		;0xcb1d        		#size:2
RR [HL]                  		;0xcb1e        		#size:2
RR [IX + 0x7F]       		;0xddcb7f1e       		#size:4
RRA                      		;0x1f          		#size:1
RRC A                    		;0xcb0f        		#size:2
RRC B                    		;0xcb08        		#size:2
RRC C                    		;0xcb09        		#size:2
RRC D                    		;0xcb0a        		#size:2
RRC E                    		;0xcb0b        		#size:2
RRC H                    		;0xcb0c        		#size:2
RRC L                    		;0xcb0d        		#size:2
RRC [HL]                 		;0xcb0e        		#size:2
RRC [IX + 0x7F]       		;0xddcb7f0e       		#size:4
RRCA                     		;0x0f          		#size:1
RRD                      		;0xed67        		#size:2
RST 0                    		;0xc7          		#size:1
RST 10                   		;0xd7          		#size:1
RST 18                   		;0xdf          		#size:1
RST 20                   		;0xe7          		#size:1
RST 28                   		;0xef          		#size:1
RST 30                   		;0xf7          		#size:1
RST 38                   		;0xff          		#size:1
RST 8                    		;0xcf          		#size:1
SBC A, 0xaa              		;0xdeaa        		#size:2
SBC A, A                 		;0x9f          		#size:1
SBC A, B                 		;0x98          		#size:1
SBC A, C                 		;0x99          		#size:1
SBC A, D                 		;0x9a          		#size:1
SBC A, E                 		;0x9b          		#size:1
SBC A, H                 		;0x9c          		#size:1
SBC A, L                 		;0x9d          		#size:1
SBC A, XH                		;0xdd9c        		#size:2
SBC A, XL                		;0xdd9d        		#size:2
SBC A, YH                		;0xfd9c        		#size:2
SBC A, YL                		;0xfd9d        		#size:2
SBC A, [HL]              		;0x9e          		#size:1
SBC A, [IX + 0x7F]       		;0xfd9e7f       		#size:3
SBC HL, BC               		;0xed42        		#size:2
SBC HL, DE               		;0xed52        		#size:2
SBC HL, HL               		;0xed62        		#size:2
SBC HL, SP               		;0xed72        		#size:2
SCF                      		;0x37          		#size:1
SET 0, A                 		;0xcbc7        		#size:2
SET 0, B                 		;0xcbc0        		#size:2
SET 0, C                 		;0xcbc1        		#size:2
SET 0, D                 		;0xcbc2        		#size:2
SET 0, E                 		;0xcbc3        		#size:2
SET 0, H                 		;0xcbc4        		#size:2
SET 0, L                 		;0xcbc5        		#size:2
SET 0, [HL]              		;0xcbc6        		#size:2
SET 0, [IX + 0x7F]       		;0xddcb7fc6       		#size:4
SET 1, A                 		;0xcbcf        		#size:2
SET 1, B                 		;0xcbc8        		#size:2
SET 1, C                 		;0xcbc9        		#size:2
SET 1, D                 		;0xcbca        		#size:2
SET 1, E                 		;0xcbcb        		#size:2
SET 1, H                 		;0xcbcc        		#size:2
SET 1, L                 		;0xcbcd        		#size:2
SET 1, [HL]              		;0xcbce        		#size:2
SET 1, [IX + 0x7F]       		;0xddcb7fce       		#size:4
SET 2, A                 		;0xcbd7        		#size:2
SET 2, B                 		;0xcbd0        		#size:2
SET 2, C                 		;0xcbd1        		#size:2
SET 2, D                 		;0xcbd2        		#size:2
SET 2, E                 		;0xcbd3        		#size:2
SET 2, H                 		;0xcbd4        		#size:2
SET 2, L                 		;0xcbd5        		#size:2
SET 2, [HL]              		;0xcbd6        		#size:2
SET 2, [IX + 0x7F]       		;0xddcb7fd6       		#size:4
SET 3, A                 		;0xcbdf        		#size:2
SET 3, B                 		;0xcbd8        		#size:2
SET 3, C                 		;0xcbd9        		#size:2
SET 3, D                 		;0xcbda        		#size:2
SET 3, E                 		;0xcbdb        		#size:2
SET 3, H                 		;0xcbdc        		#size:2
SET 3, L                 		;0xcbdd        		#size:2
SET 3, [HL]              		;0xcbde        		#size:2
SET 3, [IX + 0x7F]       		;0xddcb7fde       		#size:4
SET 4, A                 		;0xcbe7        		#size:2
SET 4, B                 		;0xcbe0        		#size:2
SET 4, C                 		;0xcbe1        		#size:2
SET 4, D                 		;0xcbe2        		#size:2
SET 4, E                 		;0xcbe3        		#size:2
SET 4, H                 		;0xcbe4        		#size:2
SET 4, L                 		;0xcbe5        		#size:2
SET 4, [HL]              		;0xcbe6        		#size:2
SET 4, [IX + 0x7F]       		;0xddcb7fe6       		#size:4
SET 5, A                 		;0xcbef        		#size:2
SET 5, B                 		;0xcbe8        		#size:2
SET 5, C                 		;0xcbe9        		#size:2
SET 5, D                 		;0xcbea        		#size:2
SET 5, E                 		;0xcbeb        		#size:2
SET 5, H                 		;0xcbec        		#size:2
SET 5, L                 		;0xcbed        		#size:2
SET 5, [HL]              		;0xcbee        		#size:2
SET 5, [IX + 0x7F]       		;0xddcb7fee       		#size:4
SET 6, A                 		;0xcbf7        		#size:2
SET 6, B                 		;0xcbf0        		#size:2
SET 6, C                 		;0xcbf1        		#size:2
SET 6, D                 		;0xcbf2        		#size:2
SET 6, E                 		;0xcbf3        		#size:2
SET 6, H                 		;0xcbf4        		#size:2
SET 6, L                 		;0xcbf5        		#size:2
SET 6, [HL]              		;0xcbf6        		#size:2
SET 6, [IX + 0x7F]       		;0xddcb7ff6       		#size:4
SET 7, A                 		;0xcbff        		#size:2
SET 7, B                 		;0xcbf8        		#size:2
SET 7, C                 		;0xcbf9        		#size:2
SET 7, D                 		;0xcbfa        		#size:2
SET 7, E                 		;0xcbfb        		#size:2
SET 7, H                 		;0xcbfc        		#size:2
SET 7, L                 		;0xcbfd        		#size:2
SET 7, [HL]              		;0xcbfe        		#size:2
SET 7, [IX + 0x7F]       		;0xddcb7ffe       		#size:4
SLA A                    		;0xcb27        		#size:2
SLA B                    		;0xcb20        		#size:2
SLA C                    		;0xcb21        		#size:2
SLA D                    		;0xcb22        		#size:2
SLA E                    		;0xcb23        		#size:2
SLA H                    		;0xcb24        		#size:2
SLA L                    		;0xcb25        		#size:2
SLA [HL]                 		;0xcb26        		#size:2
SLA [IX + 0x7F]       		;0xddcb7f26       		#size:4
SLL A                    		;0xcb37        		#size:2
SLL B                    		;0xcb30        		#size:2
SLL C                    		;0xcb31        		#size:2
SLL D                    		;0xcb32        		#size:2
SLL E                    		;0xcb33        		#size:2
SLL H                    		;0xcb34        		#size:2
SLL L                    		;0xcb35        		#size:2
SLL [HL]                 		;0xcb36        		#size:2
SLL [IX + 0x7F]       		;0xddcb7f36       		#size:4
SRA A                    		;0xcb2f        		#size:2
SRA B                    		;0xcb28        		#size:2
SRA C                    		;0xcb29        		#size:2
SRA D                    		;0xcb2a        		#size:2
SRA E                    		;0xcb2b        		#size:2
SRA H                    		;0xcb2c        		#size:2
SRA L                    		;0xcb2d        		#size:2
SRA [HL]                 		;0xcb2e        		#size:2
SRA [IX + 0x7F]       		;0xddcb7f2e       		#size:4
SRL A                    		;0xcb3f        		#size:2
SRL B                    		;0xcb38        		#size:2
SRL C                    		;0xcb39        		#size:2
SRL D                    		;0xcb3a        		#size:2
SRL E                    		;0xcb3b        		#size:2
SRL H                    		;0xcb3c        		#size:2
SRL L                    		;0xcb3d        		#size:2
SRL [HL]                 		;0xcb3e        		#size:2
SRL [IX + 0x7F]       		;0xddcb7f3e       		#size:4
SUB 0xaa                 		;0xd6aa        		#size:2
SUB A                    		;0x97          		#size:1
SUB B                    		;0x90          		#size:1
SUB C                    		;0x91          		#size:1
SUB D                    		;0x92          		#size:1
SUB E                    		;0x93          		#size:1
SUB H                    		;0x94          		#size:1
SUB L                    		;0x95          		#size:1
SUB XH                   		;0xdd94        		#size:2
SUB XL                   		;0xdd95        		#size:2
SUB YH                   		;0xfd94        		#size:2
SUB YL                   		;0xfd95        		#size:2
SUB [HL]                 		;0x96          		#size:1
SUB [IX + 0x7F]       		;0xfd967f       		#size:3
XOR 0xaa                 		;0xeeaa        		#size:2
XOR A                    		;0xaf          		#size:1
XOR B                    		;0xa8          		#size:1
XOR C                    		;0xa9          		#size:1
XOR D                    		;0xaa          		#size:1
XOR E                    		;0xab          		#size:1
XOR H                    		;0xac          		#size:1
XOR L                    		;0xad          		#size:1
XOR XH                   		;0xddac        		#size:2
XOR XL                   		;0xddad        		#size:2
XOR YH                   		;0xfdac        		#size:2
XOR YL                   		;0xfdad        		#size:2
XOR [HL]                 		;0xae          		#size:1
XOR [IX + 0x7F]       		;0xfdae7f       		#size:3