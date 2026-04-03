<?php
/**
 * Probe script for three-engine comparison testing.
 * Outputs a stable JSON with server variables, headers, and PHP config.
 * Marker: PROBE_V2
 */
header('Content-Type: application/json; charset=utf-8');
header('X-Probe-Marker: PROBE_V2');

$vars = [
    'PROBE_MARKER'    => 'PROBE_V2',
    'REQUEST_URI'     => $_SERVER['REQUEST_URI'] ?? null,
    'QUERY_STRING'    => $_SERVER['QUERY_STRING'] ?? null,
    'SCRIPT_NAME'     => $_SERVER['SCRIPT_NAME'] ?? null,
    'SCRIPT_FILENAME' => $_SERVER['SCRIPT_FILENAME'] ?? null,
    'PHP_SELF'        => $_SERVER['PHP_SELF'] ?? null,
    'PATH_INFO'       => $_SERVER['PATH_INFO'] ?? null,
    'REDIRECT_URL'    => $_SERVER['REDIRECT_URL'] ?? null,
    'REDIRECT_STATUS' => $_SERVER['REDIRECT_STATUS'] ?? null,
    'HTTP_HOST'       => $_SERVER['HTTP_HOST'] ?? null,
    'HTTPS'           => $_SERVER['HTTPS'] ?? null,
    'REQUEST_METHOD'  => $_SERVER['REQUEST_METHOD'] ?? null,
    'REMOTE_ADDR'     => $_SERVER['REMOTE_ADDR'] ?? null,
    'SERVER_SOFTWARE'  => $_SERVER['SERVER_SOFTWARE'] ?? null,
    // Environment variables set by SetEnv/SetEnvIf
    'TEST_ENV'        => $_SERVER['TEST_ENV'] ?? getenv('TEST_ENV') ?: null,
    'CUSTOM_VAR'      => $_SERVER['CUSTOM_VAR'] ?? getenv('CUSTOM_VAR') ?: null,
    'BROWSER_FLAG'    => $_SERVER['BROWSER_FLAG'] ?? getenv('BROWSER_FLAG') ?: null,
    // PHP INI values (verify php_value directives)
    'php_memory_limit'       => ini_get('memory_limit'),
    'php_display_errors'     => ini_get('display_errors'),
    'php_max_execution_time' => ini_get('max_execution_time'),
    // GET/POST data
    'GET'  => $_GET ?: null,
    'POST' => $_POST ?: null,
];

echo json_encode($vars, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
