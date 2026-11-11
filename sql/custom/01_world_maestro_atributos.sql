-- Maestro de Atributos (sistema_atributos.cpp) - se puede ejecutar varias veces
USE `world`;
SET SESSION sql_mode = '';

DELETE FROM `creature_template` WHERE `entry` = 500100;
DROP TEMPORARY TABLE IF EXISTS `tmp_maestro`;
CREATE TEMPORARY TABLE `tmp_maestro` AS SELECT * FROM `creature_template` WHERE `entry` = 35642; -- Jeeves como base (modelo robot)
UPDATE `tmp_maestro` SET
    `entry` = 500100,
    `name` = 'Maestro de Atributos',
    `subname` = 'Reparte tus puntos',
    `npcflag` = 1,
    `faction` = 35,
    `minlevel` = 90, `maxlevel` = 90,
    `AIName` = '',
    `ScriptName` = 'npc_maestro_atributos',
    `gossip_menu_id` = 0;
INSERT INTO `creature_template` SELECT * FROM `tmp_maestro`;
DROP TEMPORARY TABLE IF EXISTS `tmp_maestro`;

DELETE FROM `creature_template_locale` WHERE `entry` = 500100;
INSERT INTO `creature_template_locale` (`entry`, `locale`, `Name`, `FemaleName`, `SubName`, `VerifiedBuild`) VALUES
(500100, 'esES', 'Maestro de Atributos', NULL, 'Reparte tus puntos', 0),
(500100, 'esMX', 'Maestro de Atributos', NULL, 'Reparte tus puntos', 0);

DELETE FROM `command` WHERE `name` = 'atributos';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('atributos', 0, 'Sintaxis: .atributos  - Convoca al Maestro de Atributos.\n.atributos resumen - Muestra tus puntos.');
