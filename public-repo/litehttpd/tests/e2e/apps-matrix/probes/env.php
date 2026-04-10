<?php
header('Content-Type: application/json; charset=utf-8');
header('X-Probe-Marker: APPS_MATRIX_V1');

$keys = [
    'TEST_ENV',
    'CUSTOM_VAR',
    'BROWSER_FLAG',
    'HTTPS',
    'REQUEST_URI',
    'QUERY_STRING',
];

$payload = ['PROBE_MARKER' => 'APPS_MATRIX_V1'];
foreach ($keys as $key) {
    $payload[$key] = $_SERVER[$key] ?? getenv($key) ?: null;
}

echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
