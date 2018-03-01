/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_drawer.h"

#include "third_party/imgui/imgui.h"
#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

// File: 'ProggyTiny.ttf' (35656 bytes)
// Exported using binary_to_compressed_c.cpp
const char kProggyTinyCompressedDataBase85[10950 + 1] =
    R"(7])#######LJg=:'/###[),##/l:$#Q6>##5[n42<Vh8H4,>>#/e>11NNV=Bv(*:.F?uu#(gRU.o0XGH`$vhLG1hxt9?W`#,5LsCm<]vf.r$<$u7k;hb';9C'mm?]XmKVeU2cD4Eo3R/[WB]b(MC;$jPfY.;h^`ItLw6Lh2TlS+f-s$o6Q<BaRTQrU.xfLq$N;$0iR/G0VCf_cW2p/W*q?-qmnUCLYgR`*1mTi+7.nT@C=GH?a9wps_2IH,.TQg1)Q-GL(lf(T(ofL:%SS%MS=C#jfQ$X7V$t'X#(v#Y9w0#2D$CI]V3N0PRAV3#&5>#X14,MZ[Z##UE31#J&###Q-F%b>-nw'w++GM-]u)Nx0#,M[LH>#Zsvx+6O_^#l(FS7f`C_&E?g'&kcg-6Y,/;M#@2G`Bf%=(`5LF%fv$8#,+[0#veg>$EB&sQSSDgEKnIS7EM9>Z,KO_/78pQWqJE#$nt-4$F&###E`J&#uU'B#*9D6N;@;=-:U>hL&Y5<-%A9;-Y+Z&P^)9<QYN8VQM#S/Mx?c(NdBxfMKpCEPX;*qM$Q?##&5>##._L&#awnk+ib*.-Z06X1>LcA#'rB#$o4ve6)fbA#kt72LN.72L=CG&#*iX&#90Wt(F,>>#_03:)`(@2L@^x4Sj2B@PN#[xO8QkJNRR()N@#f.Mr#)t-L5FGMm8#&#/2TkLi3n##-/5##MwQ0#EB^1v&ols-)-mTMQ@-##qlQ08*lkA#aNRV7KRYML%4_s[kNa=_0Z%7Nd4[3#S@1g_/v`W#'`Fm#<MOe#_=:n#Lx;%$b(w,$g&J1$N9>B$(Q#n$oqvc$&Svv$`,TM%,PS=%OeJE%s+]l%A=Fe%']K#&7aW5&O-Nd&q&>^&GZs1'w.bA'c>u>'B-1R'%gJ.(t1tx'_jH4(iNdc(GJ*X(l`uf(^Wqr(-=Jx(=[%5)')Gb)$1vV)57Vk),8n<*BYl/*qs%]*OI5R*Fkgb*H<+q*TQv(+Xak6+?C@H+5SaT+o2VhLKd)k+i$xl+4YW=,sJd,,C*oT,Eb:K,mSPgLsF8e,Z$=rJ[<5J`E:E&#k&bV7uVco]JfaJ2M'8L#xArJ27FJx?Zgt%uov/vZ@?Gj:Kl;,jo%*K;AL7L#7G'3/J(*t.5xO+/0r+N/%ipJ/Bq_k/A>4Y/^iwl/%K:K0[HW=04D'N0wQq_00Kjt0]NJ21?p?d1T:=Y1e*&i1HLr@28x*:29A[L2Mpd%3pFIp2igO+3aXRX3M#PN3uY$d37p2=4c,s54.3SI4v0iw4JqN65G$S*5rh<65ld7E5.IRt5.f-16A/U(6IoFR6Nj7I6Y3i[6>s#s6EF=P90>=W6-Mc##=(V$#MXI%#^3=&#nd0'#(?$(#8pm(#HJa)#X%T*#iUG+##1;,#3b.-#C<x-#Smk.#GdrI3TCR/$3Ds9)?^k-$&pG/?Hn.1#rPr.LR;NHZYu-A-muPG`uqJfLK_v>#$i0B#'2[0#s6aW-AS*wp1W,/$-pZw'%]#AOC+[]O>X`=-9_cHMN8r&MsKH##77N/)8r_G3=^x]O].[]-/(pI$^=Kn<00k-$t`%/LDK5x76,G&#$or>I?v+sQ;koJM>,CS-14,dM,Hv<-cLH?01FQ*NGx0='H9V&#;Rov$8ooX_i7d;)]]>R*sVi.Lt3NM-$@dXM:uSGMDn%>-30[b's6Ct_.39I$3#bo7;FP&#YKh9&#d)KE$tok&L1tY-sTf2LP]K<Lsjr>&s9L]u-c4Au9*A>-<'3UN-PZL-NIV+85p0eZ3:.Q8bj1S*(h)Z$lel,MX_CH-.Nck-(veHZwdJe$ej+_frio0cKB$HFtRZ>#DiaWqFq7Q84okA#tiUi'Qumo%<]Xl8As(?@iLT[%tDn8gsDGA#hDu-$+HM3X_?@_8:N+q7v3G&#a7>0H3=t-?ZKm.HK+U58E/.`AcQV,tUd+Z-$fQ-Haotl8Zx2Fn)&UQ8c6E&docd.%&^R]u)x:p.N*wIL8+fsrk+5<MR@v58X^?xKxUi^6A``6MU-lPSgJ$##P*w,v%,[0#Rhi;-`2$I%*nhxu67Np.(AP##Y+YB]LD_K*NPG])IsiA#Dqi05siIfL;G;QMM8-##?bu&#,>###>jq:9%/v2;f`?J8fDrG%fmWw9gl'ENgjG:,EC%<-WW5x'6eaR86kf2`5alP&u]::.'a0i);c)3LN3wK#gZb19YvMa,?IggL3xoFMTK_P85<B9&NP'##mF#m8$6<QhEn>.)0xLp7gw]m_oM++.`=JfLm)1#.gGKd4N^@N%M'Np7ZO:k)VTqt%EO`gurjj;-0r%;%I<Ga>'M,W-(hdnXP4bA,%GLp75c<LYo5oMiXKh+0O>`QUWh<_&.ZoDuWmL<LKx(B6eVxZ9,V@Z76OM=-Ke??.]RXk:UD`?%^FHM&LMQY-SJmDc?1&Z$gq`gMi.(58gkcA#l5#N9#9Z;%Y*K-;8K?E5#0]guh&tP8m7:f[<f568<JtpBUNiF*4db;-[s[n&9o`Y-R7B$L4*XQ8t$,?.Vqa_&fQB?-/]2u$#JUp7S+5wp=25?R6W5@MA)jB%lpNp7^'9U)jNtKNBU0I,'XFQ/&&###'><h#I[*T.73rI3#1[m%:TUv[NC90/Q]i.L(dt_/1dC_&8QFeFKgL<L+qdU2f$;R3rftK3GiIn&ddcA,CDkGM'CYcM#c[#%(MgTTc645&L(T&#b:o<<l/tDYp$M3<QQGb@vjfe$i@nEI?ZKal44)=-T4sP-u0@q$:-d9`EQjDNuagC-_1X_$PQ`&#g1iJs&h'a)J`C<-M`B-'sB1tL>CVJ+7:P&#Wj7n$+8sb<:+R.Qx7m<-T`&0%3TK<-h.oN'eSYW-g7D^6mu<W)>7Rc;:cIH%5hWHX9uCq'RC/2'GZZ(=:.$ekS>k((WP_=-,8dT%;]DeHjNJ'HOsgj-vUa$UFQO68Ic+k2HwQ'(0Kgn8V=:</jUcP(Nir;JdYO&/+mZe;Cmw@^[x8IH2i<w-u$Hq7lB)KE@V)7)'R4tQc*Fv%0DTgLvgjIL%Xi.Lb+pE='Rf3M_o>*(iM]?[]-#9)#tb,;mdUw.SB+-8M*cj)1)A@;:O#kipW<78t=vERat3KN(RZ&%0)3XJh/1q+<E3'mJ9?m'as868qukA#>'_5'r1GX`4;kNbkh&@-HCp[+c+Z68=Z9:BM#Jn$R+0Au6A)K:YXr1d^8ILE65V'#Y_%n8Mc`3r:>H9%PMhj9GVCh3F3wm81EG&#,`**<3AEYLN1pA#>q0p&(^?@'Bl+&>klY'vO%co7juS^d)a#9%=&m9.m`0i)rQNm8*GF]uI9+W-wmw5_L07xLC8qT;`9%90i^Gx'abQp7)>5wLq3n0#/U0V8+B^G3%3,0:3w<W8.p?#+Hp]p7*MXK3JhpgLE-q8P&(mW-dxr>R]4fJ(d%2N9Z-r_&7rjQS<XpW&&A-W--@;qi&29tUa5N&#gB)gL8,Ap-elVKW5TwR*0l;wPAjbS(()Eb<iU(Y&3:8,($2)49;(fvnOpTx=Jx#6qqjL<LA?*l2PS&i;d2W&HBfj.L6$[J8b(%df[2Q:8bDew'2N#k9gbY2VAHX*2L0,rBcSWf%AZbqKY9g8)k4ZQ_8dP^0#$$3:,hwW&^?Ie-:Z&Eu:RL>,sM;n9g#>ve^2SK)71JTRxD)o0@1wWA2#E;<PRZ;%>xov>0f^-QQQYVBeT+?-7kMD5d0B#QZAW0:Z<A^HCkC&Oe4LI89xAs997Um.Xi,FNt-iE)6=nm8<>jUK[OZi&61L&#>CCj;r]/RLH'(j>+$P-R9bF69`%f@[p-JZ*.hnp7;-ge$NSi?-qx8;-V<ZA+1q8N9tGRWAv9j+(7=DHFC=[Z:UVgY(=5)N<)b)OBsUeA,RgI#P76ZE,3tQnTSSff&N,76LMX[r;%'1'#(AP##r-7G;4akA[ve^@%sVi.LS_r&&;4qgL>]Z##,B?nrCn,)'(Q%a-sI^W&9'i&#SrRfL`Zwe%k.jA,xf:-%<Gf&#_:JfL:JQi*c/Z)<'7(a6g/mx'aPc689TO]uo<MU'5+WZPi.cE<g(_>$+:t>-v^)'%of?pg=`N_*o'w<LJb*=-q`6]'Fh0BI@9[&%7bI4VM$D*'C[:RaFCI<-v=B[%7hep7=wRLa#E-v.K#gmA.2(LNqLC2)bqDp7.5HZAm;&_8ekx;8FmR/&mTV:@#CTp9:td>)3(ip7XqF]uN-Fj9l=K/+sAH^*I=5qBCRt-,T163BO%ov7%,sb&T=XaZ$(#GM0#Qp%a]Cs7HNbxum=g@>wb%?7N:Fk'0PYRhUv-tLWr+P(lLM/:9N*H=KRZT'Pf2;.@2<)#pVl1MwLk0&;tUAuP3w.Le.]T/*Mc##O->>#9NCU.73rI3ZbA;%^xT3BS2L#$uLjf%53Kt-2SJMBFZ.m0cmcPS)aX%(c]Yg<^[G6;$W(8*2&$X->B+kk^$D'8E@P&#I-nT'u5pm8u;Be=AJ8F-T6po)A:&?-CPcd$rDtJjLUsv'7Hx_onecgHu78k:D#]4;tb)$-UHAm8h;2c>8J<@.(W=p&oVoY?&@+w7-)ri'bb=+<b2:*S]stQ_=5>X:<Q(Ka=4)=-+'h&,:TKs-#>#29.*DW/tNqT&QAl29xj+AuD:*+lnW]D,3l6<-PX9YYw)vX&=WuT8H=AbIs[`Am2xcW-jqbn*cZV%_t/Z&QpvGJ(i2.^==iWDurfn:Ml;-##/-U%)x$+1:lROdt*mpM=i4/)Zdr'H'P[N>-EKHl$hUvf:P'Q3`u*IM&uZA39^0F[pUB+n8hq+?I`L'-)2>Cq71g/6(?(oR&iBRiLr7w;-[HuL3u6e2&V:QjBJ:9iuF8.a<ARjp'0Abr&l&:P9L3B:^7aj(8PK:(QkKLTMsCt?'Yqkd4'DW-2%^Bq7xR[[%i_Nh11uZp7LW^G3(Y0(=DRYF%#jl<-h&*u$;S^,Mw?<K**]I^FG,614Dd@N%$;Ed;0pkKNJl:a%rL@Y-&5n0#TD,##%5w.LCn^-)uH90:H;lA#;qp1(J7rLExpE]F=%,(HFI8V%095g)3fBemf@#kGO5###'5>##PHT<-.4r1&,qBE<es9B#LG'>#LK,W-fIO4kX@%%#tUB#$57>uHN^KeX'-cD)d.s*<i5qHQhe%D<KIF&#_UK]u**<gLY7<C0t#jgLqWQ>#P<Eh#$`b3(.hFQ/0rC$#P9cY#]TJLC,=5(H)L2Y&SE^=-6hk.?lG8<9c6Bt+=B`&#Ee<wTFMGT&2P###3XG$>2+c&#ok:/:3l(RsPD###2d#<-%,.t$5@HgL/mu<+PXhv[Bgb4)GO;eMZQMr?,tXvIIe;t-P2l?G2j1v^)3l$'mEa68K1l@7.`V[GG#)C]Y&f;]?OM>&x]i.L(/5##BO+k9Xp0B#NS9>#+7E<-d]nl$Yw6v9YK*6(sxGug]oko$_'l_-Ai%RMq<&_8o2@2L@@AS)c8(<-c&r]$J9oq7g?(m9LIS;H-)KfF@qVI*^ACO9fKc'&6k/q7RD&Fe&*l2LSQUV$vC#W-lwf&v:'n]%D4xVH4&(^#0jg<-@r'29EQT_6Gx)Q&':nKC>s6.6*;X^ZH.->#>atJC6`hJ>NjO?-^5l-8Tj72LFIRp7,:<Q3$)F/)5Wu>-.wruM;q0)(YHWp7@i('#'2,##8ZDK<cUb5A@]kA#9fRjNh6]0#&P[g)P`0i)0d](%^m[v$q)TVHKS9.v<%SR8<<9*#2<_C-7)hg'Orqs-QEsFMp=h19vO%o<M5,##n[li98-aQL[3=G%6J>M9dvR3kYKd&#V(f+MR?xK#liDE<[/RM3M7-##1na_$+q)'%xheG*DsXbN^BxK#R90%'vrIfL.r&/LT*=(&A's2;O56>>/jsX_Z_+/(NSi.L>jEG']iNUR238^&?MR49ne<Gm'IVQ(((wQ8*9Xd=.H6h'lv049,uPGG:Jw_-Oghr?PvblrD)TJGlq=42p_N^-e59I$%]4d<r%wd+^1Iu'n94395H72LcojA#^QWp7i0/kXEFEm8TjjM3j^an8JUbA#FS0a=l6G]uVMHW-P-t2DpAqa)1xF&#l.HB,)'sFl=TA_%6DPT^$ok%(vc<'#Z'[0#SHW-$2W(1:C,QD.Ln5%()ocjK+uZH+o[2n<rh/:)jPHmk0S_GDMgTd4o'1c&X(ek90sTn`pij7'7_j:HY/6ElAGdvDUpK#$mgJH+.`]&=*6J_60lSc&>A>.M5N=h#=eq:T?k)/:;SF&#;k-gLXL0e,e6JdDNHj?@ihvi&wNT-;6`E.FAl):%3WK<-:EI+'^([:.+lQS%?_,c<8L[W&T7-##`QHXA=(xn&Yqbf(kg<LWVZx2&&]?68vN6pgnUIu'M6YE<i(72Lgs00:xi8u_)F.^'J5YY#)PjfL=w;/:ilAqBqSBt7r[b&#hEqhLJLvu#DO*e*),8Z>e2-g)[OksHT*bm+Do5IM<_jILK4Pv'=u5a*E#Z^FHmn),Dheq.+Sl##04kWoAl[W]rYHI%+d@a-.dm<%#1[,MtRBt)O(35&>-f;-J=<n)/on,48,Ut':/;3t&u5*P`^0d%5P'gLvk,6+)>.g:(xp/)U]W]+RCwgCbE0#Aes-h%vr_BF0;=K34Yv',/GoEYQQ-U&5&Aw>]ewGt?k,l$1oR8VCkF<%+nTH4Z8f%/M&dQ/(2###S`%/L*cS5JX&V_$iac=(LG:;$ZcRPA.$+bHU7-###c7^O1[qS%)S#qT=lI(#=,o_Q.^r#(w1I<-+PK`&,o'^#GhsWQt6j(,]_<g:%t)qK.h`u&QggR8p0S`ABAla*GK[,MtDNg8IUL^#'5>##IN]p7i4mFuXih@-t=58.1&>uu$&h2`2H:%'T):wPGJuD%5DJTIUXbA#pvRdM=WcO-uhKj%0ej?Ppr<A+6o*KWQJ,x)lE59)+HL$Gs2J?n(Y]9@3*?$(x%Id;IZO]un4WJ%ncg;-3Fd<-;Q*((VD,C'q3n0#J,,##Jw@D<aNO&#PYkb$S(Fk4:Ru&#()>uu9k,h%cwfi'B.x`=Tg^d4%45GM@iZQ'YGHP9(MGGG<CD?@lb`^)j()<-X,r]$5rJ&voRl5(4@m=&l#I]uFX9b<,#hdOsBqA.&?sI3w+K?I(kEe$PLRN9De:'#6]###QfB##:a;l9Z^:m9;pd,k#en3)wXN*<W.r_&Y]$O9+]^8iC,`l--hN,m%VO;&@RG&#]*J788ix?7HZU18T0Y(f;F@q7O51Llmr1hYQw<kkDqkjDx&AKu$-a&#2f<p^oXAZ$`([K:&tF?.T;a38igw-+BB]c;N)man%)(gL$V0`$ilcp7Clp/NCP,3)Ev$&41Nw`*@0.9@iN7rqS]Ll)H)1W-]3n0#`6%_8I;TGlGR1H-w,TRLb8jn'+.UE,fK^n&SO7m83wgDlG=](WrCeA%ioD(=;+<r7r)`0G8MT&#Dfdf.>vbG3jZJp7FAeEP&e<tDI$f[@Kn,$%6M.M:12f4V.,j80DmpKC;'+-tl;rn-okgdkq5%Y%pBmp@r,g2`PR-N9&<D^63_RR(L<*b*bP5<-s>PL'8k8k91/D?[-7(&(m@?q7kdH)cDfYN)@9TDSe;DG)uQh&#k+'p74N)^ohl=,'';[P9_kisBjgU,&g>Ok2=4'K%cl@Nii)3q-_.1U9,.QL/2&>uuF*^TVA7Bs&;W36AZ(j'muJG4M_<bc%_Q?6']Td`*<g-[-%u,N<Tcm(F,rGF>CpO4)0kNeFKG2V?'jkgNvkK<&MQU+<[xKVaY>/T@&Jp'HtA$a&5U&R8bs:RWYiYeQu4k(NgxE$%X6V&#X+3O-u_dQ8/_-ldRf1W-2dpGe*E^r7d>S^JisoC%s`^68r*d;))C[p7W?<McU=n%P4'Ho$8VG29mZvQ:H1[^&foZaE#jbxu$lZp7%2s&-9rJfL4s*C8mlB#$P:=QSF-*j$[A'aNtobb.Z'[0#kQ*n%:i4%(JN#,2#9bp7q>[6OfId2P&;Hr7cpB#$X8-l93rg996Nb4):v0<-Y7`[eEdoW*l/xNN9<&v,%nra*-?078.F8o8aP+Au]ZX2L:1Bn*fuW1;N&&3M5U#x'<w:u??w]RjZCNv'[c)BFnoDbPf][`*(pBQ8mcN)YW/b,'^Y(<-QIIZ*eRrP8r=24D<.#L%vXMnNG_`78f:HO*m$N$-PMR8`[9jb8tBuTW.WqG`++ho%pI<#6EZA`X;)&G;EY+Aus#XjBZXG&#j[*RBY<-El+AI':)Z.=`]4i78Z]]2`=R$m8,@^2`Qs849_LfH[(X+`)X0-2(a+@>-KA$ZAf0YS&_;AL;>g6pgV==5A6R.db1Wbs'MC9-)5u:ENe7-##H:O&=$^]712c&gL,%,cM+4(5SmwUF<qhvhO+X[b+rh(hL5:Tf$^gs]OK?Qd&kJ1>-x-*j0c-G(-9Y`N*$0_k2ece`*#JdQ8Fk=9W'&%kCjYeaHC@nL-$[d;B*<^#DuHQNYF#>Q8L*fW]8cJX/,CWR83+pVooXXk'1HCL:6I%T(`2VY(An&R8?uN]'WLJM'$/*JLXm@8JhS](6l0tv'x06oDXFC'%=7CP8fCKetbqp0%;=0iC/kRkrjK5x'qtD_Or/9x:VWF&#<r)<-rHR.'>LW`<04=q0E8/c&hItt'a8J0)kY#Q8nnV78o=6UT`-1t%-FuN:xA1J[Oq`p77%72L9$2<?vo]Q(S/,`%VaGY>-'[qp)mH^*[eL]u/o(HF0iu.-vwoW_wn=O:[3NPcDh)Zn)ex[T%LaP-VC%f$36t1CvSw,X>^>Z4q=Z58]evqT5.xP33h839>So>InZb%=w=>F%$Mdm_FjSEEwGJMN3B,-(b@mHM;uFr$r&V@-Vp;m$bF6XJbM0-'-PgQJ=Z.lBE?=x>YBPX9N3?&+0)xm'wQsH$K]MP9cmv9glREr(>=n-k;/6t$r]2@-Ips&d-8oS@pb5r@lwcQ:aum))u=KkrVv[n>Lu,@RvlOE.^Puk;v4[+9.2A2LrPn'&]/?pg&.Rq$9-vc6BUpD*8[?:BmMq*9.HFt_QSl##O->>#b7278#r%34A$;M%+=hlTsVPp'X8N&Zu/To%mDh:.,umo%5VIl90wn5F9;_OFJ?=?JbjcX($^)Rj2vao7W9Udkr[F%8:@(4F@5W5_oHOG%M4Y@G:P+JGUsRA%UeO-;Tr+OOHi8i:F$aC=K@82L(__3:>H-g)S65e;B@:xnT_x0+x,2N:rmL4)VtH#)NF7WAs,Zx'uQpE<NJEaGq^'%'j%gpB;Je(-/`%=-8`&.6X/4S-FK=f'F>U78_TX=?1s?cZYlBd'<IaN9E=Ws^iqV_,Yei68%U@9KA-Rb'2WK78hIZ;%DkE2LDfvd(M%Jn&KSC<-mSZ[$ca<@9#`'^#nx(X-BLpU@YmB#$0Q?d8/4hFco+Eu$fY%F<]%*?@FBA,;vV@-Fo:Cu047V2B18,'$Rqmr*$J4gU<7(p(Y5:wPn;v&'C(^('$9#v/1<#e+K2ta*SV0<ISF0'HPQB%oF'7F'IZ'N9$/+8Vf[VC2)&4V&7rpgL<=XD+`2aO;_((e*FKK=-J.fQ-]HGM.IhF(=2tJQ(C9ES.qL)*NpYd.:b[+Au-g([I%QL@-cVfJ8D>BugDAVB-vlc_fV5gc*s&Y9.;25##F7,W.P'OC&aTZ`*65m_&WRJM'vGl_&==(S*2)7`&27@U1G^4?-:_`=-+()t-c'ChLGF%q.0l:$#:T__&Pi68%0xi_&Zh+/(77j_&JWoF.V735&S)[R*:xFR*K5>>#`bW-?4Ne_&6Ne_&6Ne_&lM4;-xCJcM6X;uM6X;uM(.a..^2TkL%oR(#;u.T%eAr%4tJ8&><1=GHZ_+m9/#H1F^R#SC#*N=BA9(D?v[UiFk-c/>tBc/>`9IL2a)Ph#WL](#O:Jr1Btu+#TH4.#a5C/#vS3rL<1^NMowY.##t6qLw`5oL_R#.#2HwqLUwXrLp/w+#ALx>-1xRu-'*IqL@KCsLB@]qL]cYs-dpao7Om#K)l?1?%;LuDNH@H>#/X-TI(;P>#,Gc>#0Su>#4`1?#8lC?#<xU?#@.i?#D:%@#HF7@#LRI@#P_[@#Tkn@#Xw*A#]-=A#a9OA#d<F&#*;G##.GY##2Sl##6`($#:l:$#>xL$#B.`$#F:r$#JF.%#NR@%#R_R%#Vke%#Zww%#_-4&#1TR-&Mglr-k'MS.o?.5/sWel/wpEM0%3'/1)K^f1-d>G21&v(35>V`39V7A4=onx4A1OY5EI0;6Ibgr6M$HS7Q<)58UT`l8Ym@M9^/x.:bGXf:f`9G;jxp(<n:Q`<rR2A=vkix=$.JY>(F+;?,_br?0wBS@49$5A8QZlAQ#]V-kw:8.o9ro.sQRP/wj320%-ki0)EKJ1-^,,21vcc258DD39P%&4=i[]4A+=>5ECtu5I[TV6Mt587Q6mo7tB'DW-fJcMxUq4S=Gj(N=eC]OkKu=Yc/;ip3#T(j:6s7R`?U+rH#5PSpL7]bIFtIqmW:YYdQqFrhod(WEH1VdDMSrZ>vViBn_t.CTp;JCbMMrdku.Sek+f4ft(XfCsOFlfOuo7[&+T.q6j<fh#+$JhxUwOoErf%OLoOcDQ@h%FSL-AF3HJ]FZndxF_6auGcH&;Hggx7I1$BSIm/YoIrVq1KXpa._D1SiKx%n.L<U=lox/Ff_)(:oDkarTCu:.T2B-5CPgW=CPh^FCPidOCPjjXCPkpbCPlvkCPm&uCPn,(DP@t>HPA$HHPB*QHPC0ZHPD6dHPD3Q-P_aQL2<j9xpG';xpG';xpG';xpG';xpG';xpG';xpG';xpG';xpG';xpG';xpG';xpG';xpG';xpCUi'%jseUCF3K29]cP.PK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPK)uCPT$au7ggUA5o,^<-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-O<eT-RWaQ.nW&##]9Pwf+($##)";

static_assert(sizeof(ImmediateVertex) == sizeof(ImDrawVert),
              "Vertex types must match");

ImGuiDrawer* ImGuiDrawer::current_drawer_ = nullptr;

ImGuiDrawer::ImGuiDrawer(xe::ui::Window* window)
    : window_(window), graphics_context_(window->context()) {
  Initialize();
}

ImGuiDrawer::~ImGuiDrawer() {
  auto previous_state = ImGui::GetInternalState();
  ImGui::SetInternalState(internal_state_.data());
  ImGui::Shutdown();
  if (previous_state != internal_state_.data()) {
    ImGui::SetInternalState(previous_state);
  }

  current_drawer_ = nullptr;
}

void ImGuiDrawer::Initialize() {
  // Setup ImGui internal state.
  // This will give us state we can swap to the ImGui globals when in use.
  internal_state_.resize(ImGui::GetInternalStateSize());
  ImGui::SetInternalState(internal_state_.data(), true);
  current_drawer_ = this;

  auto& io = ImGui::GetIO();

  font_atlas_ = std::make_unique<ImFontAtlas>();
  io.Fonts = font_atlas_.get();

  SetupFont();

  io.DeltaTime = 1.0f / 60.0f;
  io.RenderDrawListsFn = [](ImDrawData* data) {
    assert_not_null(current_drawer_);
    current_drawer_->RenderDrawLists(data);
  };

  auto& style = ImGui::GetStyle();
  style.ScrollbarRounding = 0;
  style.WindowFillAlphaDefault = 1.0f;
  style.WindowRounding = 0;
  style.Colors[ImGuiCol_Text] = ImVec4(0.89f, 0.90f, 0.90f, 1.00f);
  style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.06f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.30f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.40f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.33f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.65f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.35f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.40f, 0.11f, 0.59f);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 0.68f, 0.00f, 0.68f);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(0.00f, 1.00f, 0.15f, 0.62f);
  style.Colors[ImGuiCol_ScrollbarGrabActive] =
      ImVec4(0.00f, 0.91f, 0.09f, 0.40f);
  style.Colors[ImGuiCol_ComboBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.99f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.90f, 0.72f, 0.50f);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
  style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.75f, 0.11f, 1.00f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.56f, 0.11f, 0.60f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.72f, 0.09f, 1.00f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.19f, 0.60f, 0.09f, 1.00f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.40f, 0.00f, 0.71f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.60f, 0.26f, 0.80f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.75f, 0.00f, 0.80f);
  style.Colors[ImGuiCol_Column] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_ColumnHovered] = ImVec4(0.36f, 0.89f, 0.38f, 1.00f);
  style.Colors[ImGuiCol_ColumnActive] = ImVec4(0.13f, 0.50f, 0.11f, 1.00f);
  style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
  style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
  style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
  style.Colors[ImGuiCol_CloseButton] = ImVec4(0.00f, 0.72f, 0.00f, 0.96f);
  style.Colors[ImGuiCol_CloseButtonHovered] =
      ImVec4(0.38f, 1.00f, 0.42f, 0.60f);
  style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.56f, 1.00f, 0.64f, 1.00f);
  style.Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogramHovered] =
      ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 1.00f, 0.00f, 0.21f);
  style.Colors[ImGuiCol_TooltipBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
  style.Colors[ImGuiCol_ModalWindowDarkening] =
      ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

  io.KeyMap[ImGuiKey_Tab] = static_cast<int>(xe::ui::KeyEvent::Key::kTab);
  io.KeyMap[ImGuiKey_LeftArrow] =
      static_cast<int>(xe::ui::KeyEvent::Key::kLeft);
  io.KeyMap[ImGuiKey_RightArrow] =
      static_cast<int>(xe::ui::KeyEvent::Key::kRight);
  io.KeyMap[ImGuiKey_UpArrow] = static_cast<int>(xe::ui::KeyEvent::Key::kUp);
  io.KeyMap[ImGuiKey_DownArrow] =
      static_cast<int>(xe::ui::KeyEvent::Key::kDown);
  io.KeyMap[ImGuiKey_Home] = static_cast<int>(xe::ui::KeyEvent::Key::kHome);
  io.KeyMap[ImGuiKey_End] = static_cast<int>(xe::ui::KeyEvent::Key::kEnd);
  io.KeyMap[ImGuiKey_Delete] = static_cast<int>(xe::ui::KeyEvent::Key::kDelete);
  io.KeyMap[ImGuiKey_Backspace] =
      static_cast<int>(xe::ui::KeyEvent::Key::kBackspace);
  io.KeyMap[ImGuiKey_Enter] = static_cast<int>(xe::ui::KeyEvent::Key::kEnter);
  io.KeyMap[ImGuiKey_Escape] = static_cast<int>(xe::ui::KeyEvent::Key::kEsc);
  io.KeyMap[ImGuiKey_A] = static_cast<int>(xe::ui::KeyEvent::Key::kA);
  io.KeyMap[ImGuiKey_C] = static_cast<int>(xe::ui::KeyEvent::Key::kV);
  io.KeyMap[ImGuiKey_V] = static_cast<int>(xe::ui::KeyEvent::Key::kV);
  io.KeyMap[ImGuiKey_X] = static_cast<int>(xe::ui::KeyEvent::Key::kX);
  io.KeyMap[ImGuiKey_Y] = static_cast<int>(xe::ui::KeyEvent::Key::kY);
  io.KeyMap[ImGuiKey_Z] = static_cast<int>(xe::ui::KeyEvent::Key::kZ);
}

void ImGuiDrawer::SetupFont() {
  auto& io = GetIO();

  ImFontConfig font_config;
  font_config.OversampleH = font_config.OversampleV = 1;
  font_config.PixelSnapH = true;
  static const ImWchar font_glyph_ranges[] = {
      0x0020,
      0x00FF,  // Basic Latin + Latin Supplement
      0,
  };
  io.Fonts->AddFontFromMemoryCompressedBase85TTF(
      kProggyTinyCompressedDataBase85, 10.0f, &font_config, font_glyph_ranges);

  // TODO(benvanik): jp font on other platforms?
  // https://github.com/Koruri/kibitaki looks really good, but is 1.5MiB.
  const char* jp_font_path = "C:\\Windows\\Fonts\\msgothic.ttc";
  if (xe::filesystem::PathExists(xe::to_wstring(jp_font_path))) {
    ImFontConfig jp_font_config;
    jp_font_config.MergeMode = true;
    jp_font_config.OversampleH = jp_font_config.OversampleV = 1;
    jp_font_config.PixelSnapH = true;
    jp_font_config.FontNo = 0;
    io.Fonts->AddFontFromFileTTF(jp_font_path, 12.0f, &jp_font_config,
                                 io.Fonts->GetGlyphRangesJapanese());
  } else {
    XELOGW("Unable to load japanese font; jp characters will be boxes");
  }

  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  font_texture_ = graphics_context_->immediate_drawer()->CreateTexture(
      width, height, ImmediateTextureFilter::kLinear, true,
      reinterpret_cast<uint8_t*>(pixels));

  io.Fonts->TexID = reinterpret_cast<void*>(font_texture_->handle);
}

void ImGuiDrawer::RenderDrawLists(ImDrawData* data) {
  auto drawer = graphics_context_->immediate_drawer();

  // Handle cases of screen coordinates != from framebuffer coordinates (e.g.
  // retina displays).
  ImGuiIO& io = ImGui::GetIO();
  float fb_height = io.DisplaySize.y * io.DisplayFramebufferScale.y;
  data->ScaleClipRects(io.DisplayFramebufferScale);

  const float width = io.DisplaySize.x;
  const float height = io.DisplaySize.y;
  drawer->Begin(static_cast<int>(width), static_cast<int>(height));

  for (int i = 0; i < data->CmdListsCount; ++i) {
    const auto cmd_list = data->CmdLists[i];

    ImmediateDrawBatch batch;
    batch.vertices =
        reinterpret_cast<ImmediateVertex*>(cmd_list->VtxBuffer.Data);
    batch.vertex_count = cmd_list->VtxBuffer.size();
    batch.indices = cmd_list->IdxBuffer.Data;
    batch.index_count = cmd_list->IdxBuffer.size();
    drawer->BeginDrawBatch(batch);

    int index_offset = 0;
    for (int j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
      const auto& cmd = cmd_list->CmdBuffer[j];

      ImmediateDraw draw;
      draw.primitive_type = ImmediatePrimitiveType::kTriangles;
      draw.count = cmd.ElemCount;
      draw.index_offset = index_offset;
      draw.texture_handle =
          reinterpret_cast<uintptr_t>(cmd.TextureId) & ~kIgnoreAlpha;
      draw.alpha_blend =
          reinterpret_cast<uintptr_t>(cmd.TextureId) & kIgnoreAlpha ? false
                                                                    : true;
      draw.scissor = true;
      draw.scissor_rect[0] = static_cast<int>(cmd.ClipRect.x);
      draw.scissor_rect[1] = static_cast<int>(height - cmd.ClipRect.w);
      draw.scissor_rect[2] = static_cast<int>(cmd.ClipRect.z - cmd.ClipRect.x);
      draw.scissor_rect[3] = static_cast<int>(cmd.ClipRect.w - cmd.ClipRect.y);
      drawer->Draw(draw);

      index_offset += cmd.ElemCount;
    }

    drawer->EndDrawBatch();
  }

  drawer->End();
}

ImGuiIO& ImGuiDrawer::GetIO() {
  current_drawer_ = this;
  ImGui::SetInternalState(internal_state_.data());
  return ImGui::GetIO();
}

void ImGuiDrawer::OnKeyDown(KeyEvent* e) {
  auto& io = GetIO();
  io.KeysDown[static_cast<int>(e->key())] = true;
  switch (e->key()) {
    case xe::ui::KeyEvent::Key::kRightShift:
    case xe::ui::KeyEvent::Key::kLeftShift: {
      io.KeyShift = true;
    } break;
    case xe::ui::KeyEvent::Key::kRightControl:
    case xe::ui::KeyEvent::Key::kLeftControl: {
      io.KeyCtrl = true;
    } break;
    default:
      break;  // do nothing
  }
}

void ImGuiDrawer::OnKeyUp(KeyEvent* e) {
  auto& io = GetIO();
  io.KeysDown[static_cast<int>(e->key())] = false;
  switch (e->key()) {
    case xe::ui::KeyEvent::Key::kRightShift:
    case xe::ui::KeyEvent::Key::kLeftShift: {
      io.KeyShift = false;
    } break;
    case xe::ui::KeyEvent::Key::kRightControl:
    case xe::ui::KeyEvent::Key::kLeftControl: {
      io.KeyCtrl = false;
    } break;
    default:
      break;  // do nothing
  }
}

void ImGuiDrawer::OnKeyChar(KeyEvent* e) {
  auto& io = GetIO();
  io.AddInputCharacter(e->key_char());
  e->set_handled(true);
}

void ImGuiDrawer::OnMouseDown(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
  switch (e->button()) {
    case xe::ui::MouseEvent::Button::kLeft: {
      io.MouseDown[0] = true;
    } break;
    case xe::ui::MouseEvent::Button::kRight: {
      io.MouseDown[1] = true;
    } break;
    default: {
      // Ignored.
    } break;
  }
}

void ImGuiDrawer::OnMouseMove(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
}

void ImGuiDrawer::OnMouseUp(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
  switch (e->button()) {
    case xe::ui::MouseEvent::Button::kLeft: {
      io.MouseDown[0] = false;
    } break;
    case xe::ui::MouseEvent::Button::kRight: {
      io.MouseDown[1] = false;
    } break;
    default: {
      // Ignored.
    } break;
  }
}

void ImGuiDrawer::OnMouseWheel(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
  io.MouseWheel += float(e->dy() / 120.0f);
}

}  // namespace ui
}  // namespace xe
