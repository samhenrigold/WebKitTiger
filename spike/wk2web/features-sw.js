// Service worker for features.html: take control at once and answer sw-ping itself.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', e => e.waitUntil(self.clients.claim()));
self.addEventListener('fetch', e => {
    if (e.request.url.endsWith('/sw-ping'))
        e.respondWith(new Response('sw-ok'));
});
