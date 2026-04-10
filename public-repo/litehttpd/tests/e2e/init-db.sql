-- =============================================================================
-- MariaDB Initialization Script for PHP Application Testing
--
-- Pre-creates databases and users for WordPress, Nextcloud, and Drupal.
-- Mounted into MariaDB container via docker-entrypoint-initdb.d.
--
-- Requirements: 13.4
-- =============================================================================

CREATE DATABASE IF NOT EXISTS nextcloud;
CREATE DATABASE IF NOT EXISTS drupal;

GRANT ALL ON wordpress.* TO 'appuser'@'%' IDENTIFIED BY 'apppass';
GRANT ALL ON nextcloud.* TO 'appuser'@'%';
GRANT ALL ON drupal.* TO 'appuser'@'%';
FLUSH PRIVILEGES;
