-- Bandas en solitario: el Buscador de bandas y los escenarios ya no exigen nivel de objeto.
-- (Las alas de banda tenian 600, un valor imposible en Pandaria.)
-- Se puede ejecutar varias veces. Para deshacer:
--   UPDATE lfg_dungeon_template t JOIN custom_lfg_ilvl_original o ON o.dungeonId = t.dungeonId SET t.requiredItemLevel = o.requiredItemLevel;
USE `world`;

CREATE TABLE IF NOT EXISTS `custom_lfg_ilvl_original` (
  `dungeonId` INT UNSIGNED NOT NULL PRIMARY KEY,
  `requiredItemLevel` SMALLINT UNSIGNED NOT NULL
) COMMENT='Nivel de objeto original del buscador de mazmorras/bandas';

INSERT IGNORE INTO `custom_lfg_ilvl_original` (`dungeonId`, `requiredItemLevel`)
SELECT `dungeonId`, `requiredItemLevel` FROM `lfg_dungeon_template` WHERE `requiredItemLevel` > 0;

UPDATE `lfg_dungeon_template` SET `requiredItemLevel` = 0 WHERE `requiredItemLevel` > 0;
