-- Orden .salircombate para cualquier jugador. Se puede ejecutar varias veces.
USE `world`;
DELETE FROM `command` WHERE `name` = 'salircombate';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('salircombate', 0, 'Sintaxis: .salircombate - Te saca del combate si quedaste trabado (10 segundos sin pelear).');
