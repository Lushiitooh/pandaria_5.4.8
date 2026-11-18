-- Orden .bufos (8 bufos de banda por 1 hora). Se puede ejecutar varias veces.
USE `world`;
DELETE FROM `command` WHERE `name` = 'bufos';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('bufos', 0, 'Sintaxis: .bufos - Te da los 8 bufos de banda (y a tu mascota) durante 1 hora.');
