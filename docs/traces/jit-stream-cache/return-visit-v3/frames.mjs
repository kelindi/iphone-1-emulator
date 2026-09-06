export function compareRaw(a,b) {
 if(a.length!==320*480*4||b.length!==a.length)throw Error('Raw frame shape mismatch');
 let pixels=0,alpha=0,maxRGB=0,minX=320,minY=480,maxX=-1,maxY=-1;
 for(let i=30*320*4;i<a.length;i+=4){const d=Math.max(Math.abs(a[i]-b[i]),Math.abs(a[i+1]-b[i+1]),Math.abs(a[i+2]-b[i+2]));maxRGB=Math.max(d,maxRGB);if(a[i+3]!==b[i+3])alpha++;if(d>2){pixels++;const x=(i/4)%320,y=Math.floor(i/4/320);minX=Math.min(minX,x);minY=Math.min(minY,y);maxX=Math.max(maxX,x);maxY=Math.max(maxY,y);}}
 return {passed:pixels===0&&alpha===0,pixelsOver2:pixels,alphaDifferences:alpha,maxRGBDifference:maxRGB,bounds:pixels?[minX,minY,maxX,maxY]:null};
}
