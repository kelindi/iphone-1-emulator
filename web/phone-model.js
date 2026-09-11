import * as THREE from './vendor/three/three.module.js';

// Model dimensions are centimeters; the 2007 iPhone is 115 × 61 × 11.6 mm.
export function createPhoneView(host, screen, input, fail) {
  const renderer = new THREE.WebGLRenderer({antialias:true, alpha:true});
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  const canvas = renderer.domElement;
  canvas.id = 'phone-3d'; canvas.tabIndex = 0;
  canvas.setAttribute('aria-label', '3D iPhone. Drag the case to rotate. Touch the screen to use the phone. Arrow keys rotate; Home resets the view.');
  host.append(canvas);
  const scene = new THREE.Scene(), phone = new THREE.Group();
  scene.add(phone);
  const camera = new THREE.PerspectiveCamera(32, 1, .1, 100);
  camera.position.z = 24;
  scene.add(new THREE.HemisphereLight(0xffffff, 0x737784, 2.5));
  for (const [x,y,z,power] of [[-8,10,12,4],[8,3,6,2],[-5,4,-10,3]]) {
    const light = new THREE.DirectionalLight(0xffffff,power);light.position.set(x,y,z);scene.add(light);
  }
  // A small local studio environment gives metal broad reflections without an HDR download.
  const studio = document.createElement('canvas');studio.width=512;studio.height=256;
  const sc=studio.getContext('2d'), gradient=sc.createLinearGradient(0,0,0,256);
  gradient.addColorStop(0,'#fafafa');gradient.addColorStop(.48,'#878b91');gradient.addColorStop(1,'#333740');
  sc.fillStyle=gradient;sc.fillRect(0,0,512,256);
  sc.fillStyle='#ffffff';sc.fillRect(45,35,50,180);sc.fillRect(280,15,100,160);
  const env=new THREE.CanvasTexture(studio);env.mapping=THREE.EquirectangularReflectionMapping;env.colorSpace=THREE.SRGBColorSpace;
  const pmrem=new THREE.PMREMGenerator(renderer), environment=pmrem.fromEquirectangular(env);
  scene.environment=environment.texture;env.dispose();pmrem.dispose();
  const chrome=new THREE.MeshStandardMaterial({color:0xd8dadd,metalness:1,roughness:.19});
  const aluminum=new THREE.MeshStandardMaterial({color:0xb8bdc2,metalness:.82,roughness:.43});
  const black=new THREE.MeshStandardMaterial({color:0x080a0d,metalness:.15,roughness:.3});
  const glass=new THREE.MeshPhysicalMaterial({color:0x07090c,metalness:.05,roughness:.19,clearcoat:1,clearcoatRoughness:.12});
  function shape(w,h,r) {
    const s=new THREE.Shape(),x=-w/2,y=-h/2;
    s.moveTo(x+r,y);s.lineTo(x+w-r,y);s.quadraticCurveTo(x+w,y,x+w,y+r);
    s.lineTo(x+w,y+h-r);s.quadraticCurveTo(x+w,y+h,x+w-r,y+h);
    s.lineTo(x+r,y+h);s.quadraticCurveTo(x,y+h,x,y+h-r);
    s.lineTo(x,y+r);s.quadraticCurveTo(x,y,x+r,y);return s;
  }
  function mesh(geometry,material,x=0,y=0,z=0) {
    const m=new THREE.Mesh(geometry,material);m.position.set(x,y,z);phone.add(m);return m;
  }
  function plate(w,h,r,depth,material,z) {
    const g=new THREE.ExtrudeGeometry(shape(w,h,r),{depth,bevelEnabled:true,bevelSegments:4,steps:1,bevelSize:.08,bevelThickness:.08,curveSegments:20});
    return mesh(g,material,0,0,z);
  }
  plate(5.92,11.32,.72,.8,aluminum,-.48);
  plate(6.0,11.4,.78,.12,chrome,.32);
  mesh(new THREE.ShapeGeometry(shape(5.84,11.24,.7),32),glass,0,0,.525);
  // Aluminum rear and the original black plastic antenna cover.
  const back=mesh(new THREE.ShapeGeometry(shape(5.92,11.32,.72),32),aluminum,0,0,-.565);back.rotation.y=Math.PI;
  const cap=new THREE.Shape();cap.moveTo(-2.96,-3.62);cap.lineTo(2.96,-3.62);cap.lineTo(2.96,-4.94);
  cap.quadraticCurveTo(2.96,-5.66,2.24,-5.66);cap.lineTo(-2.24,-5.66);cap.quadraticCurveTo(-2.96,-5.66,-2.96,-4.94);cap.closePath();
  const antenna=mesh(new THREE.ShapeGeometry(cap,32),black,0,0,-.575);antenna.rotation.y=Math.PI;
  function disc(r,material,x,y,z,backwards=false) {
    const m=mesh(new THREE.CircleGeometry(r,48),material,x,y,z);if(backwards)m.rotation.y=Math.PI;return m;
  }
  disc(.27,chrome,2.08,4.7,-.585,true);
  disc(.205,black,2.08,4.7,-.59,true);
  disc(.12,new THREE.MeshPhysicalMaterial({color:0x142b49,metalness:.6,roughness:.12,clearcoat:1}),2.08,4.7,-.595,true);
  disc(.04,new THREE.MeshBasicMaterial({color:0x6388a4}),2.04,4.75,-.6,true);
  function decal(w,h,draw,x,y,z,backwards=false) {
    const c=document.createElement('canvas');c.width=512;c.height=Math.round(512*h/w);draw(c.getContext('2d'),c.width,c.height);
    const t=new THREE.CanvasTexture(c);t.colorSpace=THREE.SRGBColorSpace;
    const m=mesh(new THREE.PlaneGeometry(w,h),new THREE.MeshBasicMaterial({map:t,transparent:true,depthWrite:false}),x,y,z);
    if(backwards)m.rotation.y=Math.PI;return m;
  }
  decal(1.15,1.4,(c,w,h)=>{
    c.scale(w/100,h/120);c.fillStyle='#666b70';
    c.beginPath();c.moveTo(50,30);c.bezierCurveTo(25,10,1,38,16,73);c.bezierCurveTo(34,115,41,91,51,94);c.bezierCurveTo(66,93,74,114,91,78);c.bezierCurveTo(67,67,69,46,89,36);c.bezierCurveTo(72,17,61,24,50,30);c.fill();
    c.beginPath();c.moveTo(51,22);c.bezierCurveTo(49,8,62,0,73,1);c.bezierCurveTo(73,14,62,24,51,22);c.fill();
  },0,2.5,-.58,true);
  decal(3.4,1.1,(c,w,h)=>{
    c.fillStyle='#666a6e';c.textAlign='center';c.font=`${w*.135}px Arial`;c.fillText('iPhone',w/2,h*.4);
    c.font=`${w*.036}px Arial`;c.fillText('Designed by Apple in California',w/2,h*.66);c.fillText('8GB',w/2,h*.86);
  },0,-2.65,-.58,true);
  const loadingScreen=document.createElement('canvas');loadingScreen.width=320;loadingScreen.height=480;
  const loadingContext=loadingScreen.getContext('2d');loadingContext.fillStyle='#000';loadingContext.fillRect(0,0,320,480);
  loadingContext.fillStyle='#a8a8ad';loadingContext.font='12px Arial';loadingContext.textAlign='center';loadingContext.fillText('Loading iPhone OS…',160,240);
  const texture=new THREE.CanvasTexture(window.iphone?.ready?screen:loadingScreen);texture.colorSpace=THREE.SRGBColorSpace;texture.minFilter=THREE.LinearFilter;texture.generateMipmaps=false;
  const display=mesh(new THREE.PlaneGeometry(4.9,7.35),new THREE.MeshBasicMaterial({map:texture,toneMapped:false}),0,.22,.55);
  mesh(new THREE.ShapeGeometry(shape(1.25,.13,.065)),black,0,4.75,.55);
  for(let i=0;i<18;i++)mesh(new THREE.PlaneGeometry(.025,.065),aluminum,-.54+i*.064,4.75,.555);
  const home=disc(.5,black,0,-4.65,.55);home.userData.button=1;
  decal(.27,.27,(c,w,h)=>{c.strokeStyle='#b8bdc4';c.lineWidth=22;c.beginPath();c.roundRect(24,24,w-48,h-48,65);c.stroke();},0,-4.65,.555).userData.button=1;
  function button(w,h,d,x,y,z,which) {
    const m=mesh(new THREE.BoxGeometry(w,h,d),chrome,x,y,z);m.userData.button=which;return m;
  }
  button(1.05,.09,.34,1.8,5.8,.05,0);
  button(.1,.44,.24,-3.08,3.95,.02,4);
  button(.1,.63,.23,-3.08,2.65,.02,2);button(.1,.63,.23,-3.08,2.0,.02,3);
  const jack=mesh(new THREE.CircleGeometry(.15,32),black,-1.8,5.75,0);jack.rotation.x=-Math.PI/2;
  const dock=mesh(new THREE.PlaneGeometry(1.45,.25),black,0,-5.75,0);dock.rotation.x=Math.PI/2;
  for(const side of [-1,1])for(let i=0;i<5;i++){
    const hole=mesh(new THREE.CircleGeometry(.045,12),black,side*(1.35+i*.18),-5.75,0);hole.rotation.x=Math.PI/2;
  }
  let active=false, frame=0, lastFrame='', dirty=true, gesture=null, zoom=1;
  const ray=new THREE.Raycaster(), pointer=new THREE.Vector2();
  function hit(e) {
    const r=canvas.getBoundingClientRect();pointer.set((e.clientX-r.left)/r.width*2-1,1-(e.clientY-r.top)/r.height*2);
    ray.setFromCamera(pointer,camera);return ray.intersectObjects(phone.children,false)[0];
  }
  function screenPoint(e) {
    const r=canvas.getBoundingClientRect();pointer.set((e.clientX-r.left)/r.width*2-1,1-(e.clientY-r.top)/r.height*2);ray.setFromCamera(pointer,camera);
    const localRay=ray.ray.clone().applyMatrix4(display.matrixWorld.clone().invert());
    const point=localRay.intersectPlane(new THREE.Plane(new THREE.Vector3(0,0,1),0),new THREE.Vector3());
    return point?{x:Math.max(0,Math.min(319,(point.x/4.9+.5)*320)),y:Math.max(0,Math.min(479,(.5-point.y/7.35)*480))}:gesture?.point;
  }
  function finish(event) {
    if(event?.pointerId !== undefined && gesture?.id !== event.pointerId)return;
    if(gesture?.kind==='touch')input({kind:'touch',phase:'up',id:gesture.id,...gesture.point});
    if(gesture?.kind==='button')input({kind:'button',which:gesture.which,pressed:false});
    gesture=null;canvas.style.cursor='grab';
  }
  canvas.addEventListener('pointerdown',e=>{
    if(gesture||e.button!==0)return;
    canvas.focus({preventScroll:true});const found=hit(e);canvas.setPointerCapture(e.pointerId);e.preventDefault();
    if(found?.object===display&&window.iphone?.ready&&!window.iphone.paused){
      gesture={kind:'touch',id:e.pointerId,point:screenPoint(e)};input({kind:'touch',phase:'down',id:e.pointerId,...gesture.point});
    }else if(found?.object.userData.button!==undefined){
      gesture={kind:'button',id:e.pointerId,which:found.object.userData.button};input({kind:'button',which:gesture.which,pressed:true});
    }else{gesture={kind:'rotate',id:e.pointerId,x:e.clientX,y:e.clientY};canvas.style.cursor='grabbing';}
  });
  canvas.addEventListener('pointermove',e=>{
    if(!gesture||gesture.id!==e.pointerId)return;
    if(gesture.kind==='touch'){gesture.point=screenPoint(e);input({kind:'touch',phase:'move',id:gesture.id,...gesture.point});}
    if(gesture.kind==='rotate'){
      phone.rotation.y+=(e.clientX-gesture.x)*.009;phone.rotation.x=THREE.MathUtils.clamp(phone.rotation.x+(e.clientY-gesture.y)*.007,-1.25,1.25);
      gesture.x=e.clientX;gesture.y=e.clientY;dirty=true;
    }
  });
  for(const name of ['pointerup','pointercancel','lostpointercapture'])canvas.addEventListener(name,finish);
  window.addEventListener('blur',finish);
  document.addEventListener('visibilitychange',()=>{if(document.hidden)finish();});
  canvas.addEventListener('wheel',e=>{
    e.preventDefault();zoom=THREE.MathUtils.clamp(zoom*Math.exp(-e.deltaY*.001),.75,1.3);resize();
  },{passive:false});
  canvas.addEventListener('keydown',e=>{
    if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home'].includes(e.key))return;
    e.preventDefault();e.stopPropagation();finish();
    if(e.key==='Home')phone.rotation.set(0,0,0);
    else {phone.rotation.y+=e.key==='ArrowLeft'?-.2:e.key==='ArrowRight'?.2:0;phone.rotation.x=THREE.MathUtils.clamp(phone.rotation.x+(e.key==='ArrowUp'?-.15:e.key==='ArrowDown'?.15:0),-1.25,1.25);}
    dirty=true;
  });
  canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();finish();active=false;cancelAnimationFrame(frame);fail();});
  function resize() {
    const {width,height}=host.getBoundingClientRect();renderer.setSize(width,height,false);camera.aspect=width/height;
    camera.position.z=Math.max(23,7.6/camera.aspect/Math.tan(THREE.MathUtils.degToRad(16))/2)/zoom;camera.updateProjectionMatrix();dirty=true;
  }
  new ResizeObserver(resize).observe(host);
  function render() {
    if(!active)return;
    if(!document.hidden){
      const ready=window.iphone?.ready, next=ready?screen.dataset.frame:'loading';
      if(next!==lastFrame){texture.image=ready?screen:loadingScreen;texture.needsUpdate=true;lastFrame=next;dirty=true;}
      if(dirty){renderer.render(scene,camera);dirty=false;}
    }
    frame=requestAnimationFrame(render);
  }
  return {
    show(){active=true;canvas.hidden=false;phone.rotation.set(-.08,-.32,0);resize();dirty=true;render();},
    hide(){finish();active=false;cancelAnimationFrame(frame);canvas.hidden=true;},
    reset(){finish();phone.rotation.set(0,0,0);zoom=1;resize();dirty=true;canvas.focus({preventScroll:true});}
  };
}
