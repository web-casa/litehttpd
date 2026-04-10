<?php
header('Content-Type: text/html; charset=utf-8');
echo '<h1>ROOT_PHP_INDEX_V1</h1>';
echo '<p>URI: ' . htmlspecialchars($_SERVER['REQUEST_URI'] ?? '') . '</p>';
