// The browser project token is public. Local development and preview visits stay out of production analytics.
const productionHosts = new Set(['2007.mobile', 'www.2007.mobile', 'slideto.run', 'www.slideto.run', 'slidetorun.vercel.app']);
const enabled = typeof window !== 'undefined' && productionHosts.has(window.location.hostname);

if (enabled) {
  // Official asynchronous PostHog snippet: analytics must never delay guest startup.
  !function(t,e){var o,n,p,r;e.__SV||(window.posthog && window.posthog.__loaded)||(window.posthog=e,e._i=[],e.init=function(i,s,a){function g(t,e){var o=e.split(".");2==o.length&&(t=t[o[0]],e=o[1]),t[e]=function(){t.push([e].concat(Array.prototype.slice.call(arguments,0)))}}p||((p=t.createElement("script")).type="text/javascript",p.crossOrigin="anonymous",p.async=!0,p.src=s.api_host.replace(".i.posthog.com","-assets.i.posthog.com")+"/static/array.js",p.onerror=function(){p=null},(r=t.getElementsByTagName("script")[0]).parentNode.insertBefore(p,r));var u=e;for(void 0!==a?u=e[a]=[]:a="posthog",u.people=u.people||[],Object.defineProperty(u,"toString",{configurable:!0,enumerable:!0,writable:!0,value:function(t){var e="posthog";return"posthog"!==a&&(e+="."+a),t||(e+=" (stub)"),e}}),Object.defineProperty(u.people,"toString",{configurable:!0,enumerable:!0,writable:!0,value:function(){return u.toString(1)+".people (stub)"}}),o="init capture register register_once register_for_session unregister unregister_for_session getFeatureFlag getFeatureFlagResult isFeatureEnabled reloadFeatureFlags updateEarlyAccessFeatureEnrollment getEarlyAccessFeatures on onFeatureFlags onSessionId getSurveys getActiveMatchingSurveys renderSurvey canRenderSurvey getNextSurveyStep identify setPersonProperties group resetGroups setPersonPropertiesForFlags resetPersonPropertiesForFlags setGroupPropertiesForFlags resetGroupPropertiesForFlags reset get_distinct_id getGroups get_session_id get_session_replay_url alias set_config startSessionRecording stopSessionRecording sessionRecordingStarted captureException loadToolbar get_property getSessionProperty createPersonProfile opt_in_capturing opt_out_capturing has_opted_in_capturing has_opted_out_capturing clear_opt_in_out_capturing debug".split(" "),n=0;n<o.length;n++)g(u,o[n]);e._i.push([i,s,a])},e.__SV=1)}(document,window.posthog||[]);
  window.posthog.init('phc_D4KWMdU4yGfvHUu8NjSqKgW94uXphpsZ3JU7nxKV4RgH', {
    api_host: 'https://us.i.posthog.com',
    defaults: '2026-05-30',
    capture_pageview: true,
    capture_pageleave: true,
    autocapture: false,
    capture_dead_clicks: false,
    capture_exceptions: false,
    disable_session_recording: true,
    person_profiles: 'identified_only',
    respect_dnt: true,
    property_denylist: ['$current_url', '$initial_current_url', '$referrer', '$initial_referrer'],
  });
  window.posthog.register({site: '2007.mobile', guest_os: 'iPhone OS 1.0', guest_build: '1A543a'});
}

// Properties at call sites contain only app lifecycle categories, timings and control names.
// Never pass guest pixels, touch coordinates, text, serial logs or backup contents here.
export function track(event, properties = {}) {
  if (!enabled) return;
  try { window.posthog.capture(event, properties); } catch { /* A blocked SDK must not interrupt the emulator. */ }
}
