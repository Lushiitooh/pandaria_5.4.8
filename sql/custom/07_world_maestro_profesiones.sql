-- Maestro de Profesiones (profesiones.cpp) y ordenes .profesiones / .dificultad. Se puede ejecutar varias veces.
USE `world`;
SET SESSION sql_mode = '';

DELETE FROM `creature_template` WHERE `entry` = 500101;
DROP TEMPORARY TABLE IF EXISTS `tmp_maestro_prof`;
CREATE TEMPORARY TABLE `tmp_maestro_prof` AS SELECT * FROM `creature_template` WHERE `entry` = 35642; -- Jeeves como base
UPDATE `tmp_maestro_prof` SET
    `entry` = 500101,
    `name` = 'Maestro de Profesiones',
    `subname` = 'Recetas, materiales y estaciones',
    `npcflag` = 129,            -- conversacion + vendedor
    `faction` = 35,
    `minlevel` = 90, `maxlevel` = 90,
    `AIName` = '',
    `ScriptName` = 'npc_maestro_profesiones',
    `gossip_menu_id` = 0;
INSERT INTO `creature_template` SELECT * FROM `tmp_maestro_prof`;
DROP TEMPORARY TABLE IF EXISTS `tmp_maestro_prof`;

DELETE FROM `creature_template_locale` WHERE `entry` = 500101;
INSERT INTO `creature_template_locale` (`entry`, `locale`, `Name`, `FemaleName`, `SubName`, `VerifiedBuild`) VALUES
(500101, 'esES', 'Maestro de Profesiones', NULL, 'Recetas, materiales y estaciones', 0),
(500101, 'esMX', 'Maestro de Profesiones', NULL, 'Recetas, materiales y estaciones', 0);

DELETE FROM `command` WHERE `name` IN ('profesiones', 'dificultad');
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('profesiones', 0, 'Sintaxis: .profesiones - Convoca al Maestro de Profesiones (aprender todo, tiendas de materiales y estaciones de trabajo).'),
('dificultad',  0, 'Sintaxis: .dificultad normal10|heroica10|normal25|heroica25|flexible - Dificultad de banda para entrar caminando.');
