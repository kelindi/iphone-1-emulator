export function graphicsFailures(serial) {
 const text=serial.map(row=>row.text).join('');
 return {restart:(text.match(/Graphics Restart/g)||[]).length,hang:(text.match(/Graphics hang/g)||[]).length};
}
export function validateManualText(action) {
 if(action.observe!=='typed-glyphs')return;
 if(typeof action.intendedText!=='string'||!action.intendedText.trim()||action.intendedText!==action.actualText)throw Error('Typed text must be explicitly nonempty and exactly match manual observation');
}
export function requireOpenInput(closed) {if(closed)throw Error('INCOMPLETE: command input closed before navigation');}
export function finalAcceptance(report) {
 report.graphicsSerial=graphicsFailures(report.serial);
 if(report.graphicsSerial.restart||report.graphicsSerial.hang)report.errors.push('Actual final serial contains a guest graphics failure');
 if(report.finished!==true)report.errors.push('INCOMPLETE: required navigation/manual gates were not finished');
 const typed=report.checks.find(row=>row.name==='typed-glyphs');
 if(!typed||typeof typed.intendedText!=='string'||!typed.intendedText.trim()||typed.actualText!==typed.intendedText)report.errors.push('No exact manual typed-glyph evidence');
 for(const name of ['home-first','safari-open','safari-reopened','address-keyboard','typed-glyphs','home-second','exact-home-repeat'])if(!report.checks.some(row=>row.name===name&&row.passed))report.errors.push('Missing required gate: '+name);
 if(report.requestFailures?.length)report.errors.push('Actual browser request failure retained');
 if(!report.finalExport?.sha256||!report.finalExport?.bytes)report.errors.push('Required final diagnostic export unavailable');
 for(const name of ['browserClosed','serverClosed','assetsExact','projectionUnchanged'])if(report[name]!==true)report.errors.push('Final closure/asset gate missing: '+name);
 report.passed=report.finished===true&&report.errors.length===0;
 return report.passed;
}
