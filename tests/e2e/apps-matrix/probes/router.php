<?php
header('Content-Type: application/json; charset=utf-8');
header('X-Probe-Marker: APPS_MATRIX_V1');

$payload = [
    'PROBE_MARKER'    => 'APPS_MATRIX_V1',
    'REQUEST_URI'     => $_SERVER['REQUEST_URI'] ?? null,
    'SCRIPT_NAME'     => $_SERVER['SCRIPT_NAME'] ?? null,
    'SCRIPT_FILENAME' => $_SERVER['SCRIPT_FILENAME'] ?? null,
    'PHP_SELF'        => $_SERVER['PHP_SELF'] ?? null,
    'PATH_INFO'       => $_SERVER['PATH_INFO'] ?? null,
    'REDIRECT_URL'    => $_SERVER['REDIRECT_URL'] ?? null,
    'REDIRECT_STATUS' => $_SERVER['REDIRECT_STATUS'] ?? null,
];

echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
