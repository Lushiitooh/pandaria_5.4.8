-- Arreglo de misiones que no se podian completar en mazmorras.
-- Se puede ejecutar varias veces.
USE `world`;
SET SESSION sql_mode = '';

-- ---------------------------------------------------------------------------
-- Monasterio Escarlata: "Unto Dust Thou Shalt Return" (31514 y 31516)
-- Al clavar las Hojas de los Ungidos en el cadaver de Whitemane, el hechizo
-- lanza el evento 33000/33001, pero ese evento no hacia nada. Ahora da el
-- credito de la mision (a todo el grupo).
-- ---------------------------------------------------------------------------
DELETE FROM `event_scripts` WHERE `id` IN (33000, 33001);
INSERT INTO `event_scripts` (`id`, `delay`, `command`, `datalong`, `datalong2`, `dataint`, `x`, `y`, `z`, `o`) VALUES
(33000, 0, 8, 64835, 1, 0, 0, 0, 0, 0),
(33001, 0, 8, 64840, 1, 0, 0, 0, 0, 0);

-- ---------------------------------------------------------------------------
-- Salas Escarlata: "Rank and File" (31490 y 31495) - matar 50 miembros de la
-- Cruzada Escarlata. Ningun enemigo daba el credito 64964.
-- ---------------------------------------------------------------------------
UPDATE `creature_template` SET `KillCredit1` = 64964
WHERE `entry` IN (59241, 59373, 59299, 59175, 58684, 58685, 58756, 58898, 59372,
                  59240, 58676, 58683, 59303, 59191, 59302, 59150, 58632)
  AND `KillCredit1` IN (0, 64964);

-- ---------------------------------------------------------------------------
-- Sima Ignea: "No Orc Left Behind" (30984) - rescatar 5 exploradores Kor'kron.
-- Los exploradores no existian en la mazmorra. Se colocan 5 junto a las jaulas;
-- al hablarles quedan rescatados.
-- ---------------------------------------------------------------------------
UPDATE `creature_template` SET `npcflag` = 1, `AIName` = 'SmartAI', `ScriptName` = '',
    `minlevel` = 16, `maxlevel` = 16, `faction` = 35
WHERE `entry` = 61680;

DELETE FROM `smart_scripts` WHERE `entryorguid` = 61680 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`,
    `event_param1`, `event_param2`, `event_param3`, `event_param4`,
    `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`,
    `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(61680, 0, 0, 1, 64, 0, 100, 0, 0, 0, 0, 0, 33, 61680, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 'Kor''kron Scout - Al hablar - Credito de rescate'),
(61680, 0, 1, 2, 61, 0, 100, 0, 0, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 'Kor''kron Scout - Al hablar - Cerrar dialogo'),
(61680, 0, 2, 0, 61, 0, 100, 0, 0, 0, 0, 0, 41, 1500, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 'Kor''kron Scout - Al hablar - Desaparecer');

DELETE FROM `creature` WHERE `guid` BETWEEN 900001 AND 900005;
INSERT INTO `creature` (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`, `modelid`, `equipment_id`,
    `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `currentwaypoint`,
    `curhealth`, `curmana`, `movement_type`) VALUES
(900001, 61680, 389, 2437, 2437, 2, 1, 0, 0, -373.90, 180.60, -21.76, 3.60, 7200, 0, 0, 1, 0, 0),
(900002, 61680, 389, 2437, 2437, 2, 1, 0, 0, -375.00, 184.20, -21.66, 3.60, 7200, 0, 0, 1, 0, 0),
(900003, 61680, 389, 2437, 2437, 2, 1, 0, 0, -376.50, 187.50, -21.58, 3.60, 7200, 0, 0, 1, 0, 0),
(900004, 61680, 389, 2437, 2437, 2, 1, 0, 0, -372.80, 183.40, -21.70, 3.60, 7200, 0, 0, 1, 0, 0),
(900005, 61680, 389, 2437, 2437, 2, 1, 0, 0, -374.20, 186.80, -21.62, 3.60, 7200, 0, 0, 1, 0, 0);
