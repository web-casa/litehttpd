<?php
header('Content-Type: application/json; charset=utf-8');
header('X-Probe-Marker: APPS_MATRIX_V1');

$keys = [
    'HTTP_HOST',
    'HTTP_USER_AGENT',
    'HTTP_REFERER',
    'HTTP_X_FORWARDED_FOR',
    'HTTP_X_FORWARDED_PROTO',
    'REQUEST_METHOD',
];

$payload = ['PROBE_MARKER' => 'APPS_MATRIX_V1'];
foreach ($keys as $key) {
    $payload[$key] = $_SERVER[$key] ?? null;
}

echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
