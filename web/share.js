// Cartão do dia em pixel art para compartilhar, carregado pelo painel do relógio só na hora de compartilhar
// (fica no site para não ocupar a flash do relógio). Gerado a partir do painel; publicado pelo web/build.js
// como panel/share-<código>.js.
(function () {
// fonte 5x7 do relógio (glcdfont do Adafruit GFX, ' ' a 'Z'): 5 colunas por letra, bit 0 em cima
const FONT='000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e080800807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43';
const pnorm=s=>String(s).normalize('NFD').replace(/[\u0300-\u036f]/g,'').replace(/[¡¿]/g,'').toUpperCase();
const ptw=(s,sc)=>pnorm(s).length*6*sc-sc;
function ptxt(g,s,x,y,sc,col){g.fillStyle=col;for(const ch of pnorm(s)){const k=ch.charCodeAt(0)-32;
 if(k>=0&&k<59)for(let cx=0;cx<5;cx++){const b=parseInt(FONT.substr((k*5+cx)*2,2),16);for(let cy=0;cy<7;cy++)if(b>>cy&1)g.fillRect(x+cx*sc,y+cy*sc,sc,sc)}x+=6*sc}}
const CARD_SPR = {"p_semente":{"p":"o3d2414 S7a4a2e sf0d58c g2e8034 G60c454 yffd23f cff8c1a v8fd16a V4f9a3a Re8402f ra8261e Dffcc33 dc48a12 wffffff","r":["................","................","................","................","................","................","................","................","................","................","................","................","......oooo......",".....oSSsSo.....","....oSSSSSSo....","...oSSSSSSSSo..."]},"p_flor":{"p":"o3d2414 S7a4a2e sf0d58c g2e8034 G60c454 yffd23f cff8c1a v8fd16a V4f9a3a Re8402f ra8261e Dffcc33 dc48a12 wffffff","r":["................","......yyy.......",".....yycyy......","......yyy.......",".......gG.......",".......gG.......",".....gGgG.......","....gGGgG.gGg...",".......gGgGGGg..",".......gG.......",".......gG.......",".......gG.......","......oooo......",".....oSSSSo.....","....oSSSSSSo....","...oSSSSSSSSo..."]},"p_verde":{"p":"o3d2414 S7a4a2e sf0d58c g2e8034 G60c454 yffd23f cff8c1a v8fd16a V4f9a3a Re8402f ra8261e Dffcc33 dc48a12 wffffff","r":["................",".......gG.......",".....gGgG.......","....gGGgGggg....",".......gGgGGg...",".......gG.vvvv..",".......gGvwvvvV.",".......gGvvvvvV.",".......gGvvvvvV.",".......gG.VVVV..","...gGg.gG.......","....gGGgG.......","......oooo......",".....oSSSSo.....","....oSSSSSSo....","...oSSSSSSSSo..."]},"p_madura":{"p":"o3d2414 S7a4a2e sf0d58c g2e8034 G60c454 yffd23f cff8c1a v8fd16a V4f9a3a Re8402f ra8261e Dffcc33 dc48a12 wffffff","r":["................",".......gG.......",".....gGgG.......","....gGGgGggg....",".......gGgGGg...",".......gG.RRRR..",".......gGRwRRRr.",".......gGRRRRRr.",".......gGRRRRRr.",".......gG.rrrr..","...gGg.gG.......","....gGGgG.......","......oooo......",".....oSSSSo.....","....oSSSSSSo....","...oSSSSSSSSo..."]},"p_dourada":{"p":"o3d2414 S7a4a2e sf0d58c g2e8034 G60c454 yffd23f cff8c1a v8fd16a V4f9a3a Re8402f ra8261e Dffcc33 dc48a12 wffffff","r":["...w.......w....","..www..gG.www...","...w.gGgG..w....","....gGGgGggg....",".......gGgGGg...",".......gG.DDDD..",".......gGDwDDDd.",".......gGDDDDDd.",".......gGDDDDDd.",".......gG.dddd..","...gGg.gG.......","....gGGgG.......","......oooo......",".....oSSSSo.....","....oSSSSSSo....","...oSSSSSSSSo..."]}};
const plant = n => CARD_SPR[['p_semente', 0, 'p_flor', 'p_flor', 'p_verde', 'p_verde', 'p_madura'][n] || 'p_dourada'];
// cartão em pixel art: desenhado numa grade de 120x150 e ampliado 9x (1080x1350, 4:5), com a fonte do
// relógio e a cena do mostrador Tomate. No celular abre o compartilhar; no PC baixa o PNG.
const WDN=[['DOM','SEG','TER','QUA','QUI','SEX','SAB'],['SUN','MON','TUE','WED','THU','FRI','SAT'],['DOM','LUN','MAR','MIE','JUE','VIE','SAB']];
const MON=[['JAN','FEV','MAR','ABR','MAI','JUN','JUL','AGO','SET','OUT','NOV','DEZ'],['JAN','FEB','MAR','APR','MAY','JUN','JUL','AUG','SEP','OCT','NOV','DEC'],['ENE','FEB','MAR','ABR','MAY','JUN','JUL','AGO','SEP','OCT','NOV','DIC']];
const MINI=['...gGg...','.rRRgRRr.','rRhRRRRRr','rRRRRRRRr','rRRRRRRRr','.rRRRRRr.','..rrrrr..'];
// o: {spr, SPR, t, LG, today, goal, streak, days}; devolve o canvas 1080x1350
window.shareCard = function (o) {
 const {spr, SPR, t, LG, today, days} = o, G = o.goal, met = today >= G, sk = o.streak || 0, j = {days};
 const SPRP = n => n == 1 ? SPR.p_broto : plant(n);
 const W=120,H=150,c=document.createElement('canvas');c.width=W;c.height=H;const g=c.getContext('2d');
 const R=(col,x,y,w,h)=>{g.fillStyle=col;g.fillRect(x,y,w,h)},ctr=(s,y,sc,col)=>ptxt(g,s,Math.round((W-ptw(s,sc))/2),y,sc,col);
 R('#0e0e10',0,0,W,H);
 R('#3a3046',3,2,W-6,H-4);R('#3a3046',2,3,W-4,H-6);R('#18141f',4,3,W-8,H-6);R('#18141f',3,4,W-6,H-8);  // cartão com cantos em degrau
 ctr('SmallTV Pomodoro',9,1,'#ff5a47');
 const now=new Date();ctr(WDN[LG][now.getDay()]+' '+now.getDate()+' '+MON[LG][now.getMonth()],19,1,'#8b8b93');
 // cena: céu em faixas, sol, nuvens, grama e terra, com a planta da sequência e o tomate
 const sx=10,sy=30,sw=100;R('#3a3046',sx-1,sy-1,sw+2,42);
 ['#2e86de','#3d91e1','#4f9de5','#64aae9','#7bb8ee','#93c8f3'].forEach((col,i)=>R(col,sx,sy+i*5,sw,5));
 R('#ffc428',sx+84,sy+4,7,7);R('#93c8f3',sx+84,sy+4,1,1);R('#93c8f3',sx+90,sy+4,1,1);R('#93c8f3',sx+84,sy+10,1,1);R('#93c8f3',sx+90,sy+10,1,1);
 R('#f4f6fa',sx+30,sy+7,12,3);R('#f4f6fa',sx+33,sy+5,6,2);R('#f4f6fa',sx+64,sy+13,10,3);R('#f4f6fa',sx+66,sy+11,5,2);
 R('#4caf50',sx,sy+30,sw,3);R('#2e7d32',sx,sy+33,sw,1);R('#5c3c2a',sx,sy+34,sw,6);
 for(let i=0;i<9;i++)R('#4a2f21',sx+3+i*11,sy+36+(i*3)%3,2,1);
 for(let i=0;i<8;i++)R('#4caf50',sx+6+i*13,sy+28,1,2);
 spr(g,SPRP(sk),sx+10,sy+1,2);spr(g,met?SPR.t_comemorando:SPR.t_feliz,sx+50,sy+2,2);
 // focos de hoje em dígitos grandes, com a meta ao lado
 const n=String(today),gl='/'+G,w1=ptw(n,3),w2=ptw(gl,2),x0=Math.round((W-w1-3-w2)/2);
 ptxt(g,n,x0,78,3,'#ffffff');ptxt(g,gl,x0+w1+3,85,2,'#5a5a66');
 ctr(met?t('Meta batida')+'!':t(today==1?'tomate hoje':'tomates hoje'),103,1,met?'#ffc83a':'#ff5a47');
 // últimos 7 dias: um tomatinho por dia (cheio = meta batida, apagado = alguns focos, contorno = nenhum)
 const last=(Array(7).fill(0).concat(j.days)).slice(-7),wd=now.getDay(),mx0=Math.round((W-7*14+5)/2);
 last.forEach((v,i)=>{const x=mx0+i*14,y=116,pal=v>=G?{g:'#60c454',G:'#2e8034',r:'#a8261e',R:'#e8402f',h:'#ff9178'}:v?{g:'#3f6a3a',G:'#3f6a3a',r:'#5a2a26',R:'#7a3a32',h:'#8a4a40'}:{g:'#3a3a44',G:'#3a3a44',r:'#3a3a44',R:'#18141f',h:'#18141f'};
  MINI.forEach((row,yy)=>[...row].forEach((ch,xx)=>{if(pal[ch])R(pal[ch],x+xx,y+yy,1,1)}));
  ptxt(g,WDN[LG][(wd-6+i+7)%7][0],x+2,126,1,i==6?'#ffffff':'#5a5a66')});
 // sequência
 if(sk){const s=sk+' '+t(sk==1?'dia seguido':'dias seguidos'),sw2=ptw(s,1)+11,x=Math.round((W-sw2)/2);spr(g,SPR.chama,x,135,1);ptxt(g,s,x+11,138,1,'#ffc83a')}
 const big=document.createElement('canvas');big.width=W*9;big.height=H*9;const bg=big.getContext('2d');bg.imageSmoothingEnabled=false;bg.drawImage(c,0,0,W*9,H*9);
 return big;
};
})();
