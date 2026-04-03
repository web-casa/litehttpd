-- Pre-create databases and user for all 4 apps
CREATE DATABASE IF NOT EXISTS wordpress;
CREATE DATABASE IF NOT EXISTS nextcloud;
CREATE DATABASE IF NOT EXISTS drupal;
CREATE DATABASE IF NOT EXISTS laravel;

CREATE USER IF NOT EXISTS 'appuser'@'%' IDENTIFIED BY 'apppass';
GRANT ALL ON wordpress.* TO 'appuser'@'%';
GRANT ALL ON nextcloud.* TO 'appuser'@'%';
GRANT ALL ON drupal.* TO 'appuser'@'%';
GRANT ALL ON laravel.* TO 'appuser'@'%';
FLUSH PRIVILEGES;
