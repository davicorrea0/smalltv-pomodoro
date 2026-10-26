const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),c=require('node:crypto'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'smalltv-sign-test-'));
const pair=()=>c.generateKeyPairSync('rsa',{modulusLength:2048,publicKeyEncoding:{type:'spki',format:'pem'},privateKeyEncoding:{type:'pkcs8',format:'pem'}});
const a=pair(),b=pair();
fs.mkdirSync(path.join(dir,'scripts'));fs.mkdirSync(path.join(dir,'smalltv-pomodoro'));
const script=path.join(dir,'scripts/assinar.cjs');fs.copyFileSync(path.join(root,'scripts/assinar.cjs'),script);
fs.writeFileSync(path.join(dir,'smalltv-pomodoro/ota-public.pem'),a.publicKey);
const key=path.join(dir,'private.pem');fs.writeFileSync(key,a.privateKey);
const file=path.join(dir,'firmware.bin'); const payload=c.randomBytes(8192); payload[0]=0xe9;fs.writeFileSync(file,payload);
const run=(cmd,files=[],k=key)=>spawnSync(process.execPath,[script,cmd,...files],{env:{...process.env,SMALLTV_SIGNING_KEY:k},encoding:'utf8'});
try {
 assert.equal(run('header').status,0); assert(!fs.readFileSync(path.join(dir,'smalltv-pomodoro/ota-public.h'),'utf8').includes('PRIVATE'));
 assert.equal(run('sign',[file]).status,0);const signed=fs.readFileSync(file+'.signed');assert.equal(signed.length,payload.length+260);
 // Independently verify exactly the core's payload/signature/LE-length format.
 assert.equal(signed.readUInt32LE(signed.length-4),256);
 assert(c.verify('sha256',payload,{key:a.publicKey,padding:c.constants.RSA_PKCS1_PADDING},signed.subarray(-260,-4)));
 assert.equal(run('verify',[file+'.signed']).status,0);
 assert.notEqual(run('verify',[file]).status,0);
 const bad=path.join(dir,'bad.bin');
 for (const mutate of [x=>{x[20]^=1;return x},x=>{x[x.length-6]^=1;return x},x=>x.subarray(0,-1),x=>Buffer.concat([x,Buffer.from([0])]),x=>{x.writeUInt32LE(0,x.length-4);return x}]) {
  fs.writeFileSync(bad,mutate(Buffer.from(signed)));assert.notEqual(run('verify',[bad]).status,0);
 }
 const wrong=path.join(dir,'wrong-private.pem');fs.writeFileSync(wrong,b.privateKey);assert.notEqual(run('sign',[file],wrong).status,0);
 const noKey=spawnSync(process.execPath,[script,'sign',file],{env:{...process.env,SMALLTV_SIGNING_KEY:''},encoding:'utf8'});assert.notEqual(noKey.status,0);
 assert.notEqual(run('verify').status,0);
 console.log('OK: ESP8266 signature format, valid, unsigned, payload/signature mutation, truncation, appended bytes, wrong length, wrong key, missing key.');
} finally { fs.rmSync(dir,{recursive:true,force:true}); }
