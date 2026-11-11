-- Oro para todos los enemigos segun su nivel + bolsas grandes al crear personaje.
-- Se puede ejecutar varias veces. Para deshacer el oro:
--   UPDATE creature_template c JOIN custom_oro_original o ON o.entry=c.entry SET c.mingold=o.mingold, c.maxgold=o.maxgold;
USE `world`;
SET SESSION sql_mode = '';

-- Promedio de oro (en cobre) de un enemigo normal por nivel, calculado de los enemigos que ya sueltan oro.
DROP TEMPORARY TABLE IF EXISTS tmp_oro_nivel;
CREATE TEMPORARY TABLE tmp_oro_nivel (nivel INT PRIMARY KEY, cobre INT);
INSERT INTO tmp_oro_nivel VALUES (1,3),(2,3),(3,3),(4,5),(5,6),(6,7),(7,7),(8,8),(9,8),(10,9),(11,13),(12,15),(13,16),(14,18),(15,21),(16,27),(17,30),(18,33),(19,36),(20,39),(21,40),(22,53),(23,59),(24,65),(25,70),(26,76),(27,77),(28,83),(29,91),(30,104),(31,112),(32,122),(33,134),(34,145),(35,151),(36,160),(37,172),(38,175),(39,194),(40,206),(41,218),(42,235),(43,247),(44,272),(45,275),(46,284),(47,298),(48,311),(49,347),(50,368),(51,391),(52,411),(53,431),(54,452),(55,455),(56,474),(57,477),(58,499),(59,538),(60,758),(61,817),(62,870),(63,900),(64,988),(65,1062),(66,1186),(67,1256),(68,1313),(69,1329),(70,1362),(71,1401),(72,1431),(73,1493),(74,1564),(75,1651),(76,1829),(77,2122),(78,2217),(79,2221),(80,2225),(81,2315),(82,2523),(83,2527),(84,2758),(85,2844),(86,2930),(87,3550),(88,3550),(89,3550),(90,3550),(91,3650),(92,3650),(93,3650);

-- Respaldo de los enemigos que se van a modificar (solo la primera vez)
CREATE TABLE IF NOT EXISTS custom_oro_original (
  entry MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY,
  mingold MEDIUMINT UNSIGNED NOT NULL,
  maxgold MEDIUMINT UNSIGNED NOT NULL
) COMMENT='Enemigos a los que se les agrego oro (valores originales)';

INSERT IGNORE INTO custom_oro_original (entry, mingold, maxgold)
SELECT c.entry, c.mingold, c.maxgold FROM creature_template c
WHERE c.mingold = 0 AND c.maxgold = 0
  AND c.npcflag = 0
  AND c.type NOT IN (8, 10, 11, 12, 13, 14, 15)       -- sin animalitos, totems, mascotas, triggers
  AND (c.unit_flags & (2 | 0x2000000)) = 0           -- atacables y seleccionables
  AND (c.flags_extra & 0x80) = 0                     -- no triggers
  AND c.maxlevel BETWEEN 1 AND 93;

-- Multiplicador por rango: normal x1, elite x2.5, raro-elite x4, jefe x6, raro x3
UPDATE creature_template c
JOIN custom_oro_original o ON o.entry = c.entry
JOIN tmp_oro_nivel n ON n.nivel = LEAST(GREATEST(c.maxlevel, 1), 93)
SET c.mingold = ROUND(n.cobre * 0.7 * CASE c.`rank` WHEN 1 THEN 2.5 WHEN 2 THEN 4 WHEN 3 THEN 6 WHEN 4 THEN 3 ELSE 1 END),
    c.maxgold = ROUND(n.cobre * 1.3 * CASE c.`rank` WHEN 1 THEN 2.5 WHEN 2 THEN 4 WHEN 3 THEN 6 WHEN 4 THEN 3 ELSE 1 END)
WHERE o.mingold = 0 AND o.maxgold = 0;

DROP TEMPORARY TABLE IF EXISTS tmp_oro_nivel;

-- Bolsas: 4 x "Foror's Crate of Endless Resist Gear Storage" (36 casillas) para todo personaje nuevo.
-- race 0 / class 0 = todas las razas y clases. El juego las equipa solas en las 4 ranuras de bolsa.
DELETE FROM playercreateinfo_item WHERE itemid = 23162;
INSERT INTO playercreateinfo_item (race, class, itemid, amount) VALUES (0, 0, 23162, 4);

SELECT COUNT(*) AS enemigos_con_oro_nuevo FROM custom_oro_original;
