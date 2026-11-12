-- Ordenes nuevas para cualquier jugador: cartas de nivel, buzon y subastador portatiles.
DELETE FROM `command` WHERE `name` IN ('cartas', 'buzon', 'subasta');
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('cartas',  0, 'Sintaxis: .cartas  - Muestra las cartas por elegir.\n.cartas elegir N - Elige la carta N (1-3).\n.cartas rehacer - Cambia las 3 cartas (1 vez por eleccion).\n.cartas lista - Tus cartas.'),
('buzon',   0, 'Sintaxis: .buzon - Aparece un buzon a tu lado durante 2 minutos.'),
('subasta', 0, 'Sintaxis: .subasta - Aparece un subastador a tu lado durante 2 minutos.');
