const stage=document.querySelector('.device-stage'), flat=document.querySelector('.phone');
const toggle=document.querySelector('#view-3d'), reset=document.querySelector('#view-reset'), hint=document.querySelector('#view-hint');
const twoD=document.querySelector('#view-2d');
let view, active=false, loading=false, unavailable=false;
const input=detail=>window.dispatchEvent(new CustomEvent('iphone-3d-input',{detail}));
function flatView(message='Slide to unlock · Touch the screen to explore') {
  view?.hide();active=false;flat.style.visibility='';flat.inert=false;stage.dataset.view='2d';
  toggle.setAttribute('aria-pressed','false');toggle.textContent='3D';reset.hidden=true;hint.textContent=message;
  twoD.setAttribute('aria-pressed','true');
}
twoD.addEventListener('click',()=>flatView());
new MutationObserver(()=>{
  toggle.disabled=loading||unavailable;
  if(active&&document.querySelector('#boot-loader').dataset.state==='error')flatView();
}).observe(document.querySelector('#boot-loader'),{attributes:true,attributeFilter:['data-state']});
toggle.addEventListener('click',async()=>{
  if(loading||active||unavailable)return;
  loading=true;toggle.disabled=true;twoD.disabled=true;toggle.textContent='Loading…';hint.textContent='Opening the 3D model…';
  try {
    if(!view){const {createPhoneView}=await import('./phone-model.js');view=createPhoneView(stage,document.querySelector('#screen'),input,()=>{unavailable=true;flatView('3D is unavailable. You can keep using the phone in 2D.');toggle.disabled=true;});}
    input({kind:'release'});view.show();flat.style.visibility='hidden';flat.inert=true;active=true;stage.dataset.view='3d';
    toggle.setAttribute('aria-pressed','true');toggle.textContent='3D';reset.hidden=false;hint.textContent='Drag the case to rotate · Touch the screen to explore';
    twoD.setAttribute('aria-pressed','false');
  }catch(error){console.error('3D view unavailable',error);stage.querySelector('#phone-3d')?.remove();flatView('3D is unavailable. You can keep using the phone in 2D.');}
  finally{loading=false;toggle.disabled=unavailable;toggle.textContent='3D';twoD.disabled=false;}
});
reset.addEventListener('click',()=>view?.reset());
