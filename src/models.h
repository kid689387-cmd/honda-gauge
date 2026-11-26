// Honda models sold in Thailand and the 3-letter model code on their ECU part
// number (38770-XXX-... / 30400-XXX-...). From the user's list; not verified
// against Honda parts data. Used to pick the bike and, later, to check that a
// remap file matches the ECU it is written to.
#pragma once

struct BikeModel { uint8_t group; const char *name; const char *variant; const char *code; };

enum { GRP_FAMILY, GRP_AUTO, GRP_SPORT, GRP_COUNT };
static const char *const groupNames[GRP_COUNT] = {"ครอบครัว", "ออโตเมติก", "สปอร์ต"};

static const BikeModel bikeModels[] = {
  {GRP_FAMILY, "Wave 110i", "ตัวแรก (2009-2010)", "KWW"},
  {GRP_FAMILY, "Wave 110i", "ท่อตรง / ไมล์ส้ม (2011-2013)", "K03"},
  {GRP_FAMILY, "Wave 110i", "โฉม 3-4 (2014-2018)", "K58"},
  {GRP_FAMILY, "Wave 110i", "ไฟหน้า LED (2019-2020)", "K58"},
  {GRP_FAMILY, "Wave 110i", "เครื่องใหม่ (2021-ปัจจุบัน)", "K1M"},
  {GRP_FAMILY, "Wave 125i", "บังลม (2005-2010)", "KPH"},
  {GRP_FAMILY, "Wave 125i", "ปลาวาฬ (2012-2017)", "KYZ"},
  {GRP_FAMILY, "Wave 125i", "ไฟหน้า LED (2018-2022)", "K73"},
  {GRP_FAMILY, "Wave 125i", "เครื่องใหม่ (2023-ปัจจุบัน)", "K2P"},
  {GRP_FAMILY, "Dream Super Cub", "ไฟกลม / ไฟเหลี่ยม (2013-2017)", "KZV"},
  {GRP_FAMILY, "Super Cub", "ไฟกลม LED (2018-2020)", "K76"},

  {GRP_AUTO, "Click 110i", "รุ่นแรก", "KVB"},
  {GRP_AUTO, "Click 125i", "โฉมแรก (2012-2014)", "KZR"},
  {GRP_AUTO, "Click 125i", "ไฟหน้า LED (2015-2017)", "K60"},
  {GRP_AUTO, "Click 125i/150i", "ไมล์ดิจิทัล (2018-2021)", "K59"},
  {GRP_AUTO, "Click 160", "4 วาล์ว eSP+ (2022-ปัจจุบัน)", "K2S"},
  {GRP_AUTO, "Scoopy i", "โฉมแรก (2009-2011)", "KYT"},
  {GRP_AUTO, "Scoopy i", "ไฟเลี้ยวบังลม (2012-2016)", "K16"},
  {GRP_AUTO, "Scoopy i", "ไฟหน้า LED (2017-2020)", "K93"},
  {GRP_AUTO, "Scoopy", "เฟรม eSAF (2021-ปัจจุบัน)", "K2F"},
  {GRP_AUTO, "Zoomer-X", "โฉมแรก / ไมล์ดิจิทัล", "K20"},
  {GRP_AUTO, "PCX 125/150", "โฉม 1-2 (2010-2013)", "KWN/KZY"},
  {GRP_AUTO, "PCX 150", "ไฟหน้า LED (2014-2017)", "K35/K36"},
  {GRP_AUTO, "PCX 150", "ไมล์ดิจิทัล (2018-2020)", "K97"},
  {GRP_AUTO, "PCX 160", "4 วาล์ว eSP+ (2021-ปัจจุบัน)", "K1Z"},
  {GRP_AUTO, "ADV 150/160", "โฉม 150 / โฉม 160", "K0W"},
  {GRP_AUTO, "Forza 300", "โฉมแรก / All New", "K04/K0B"},
  {GRP_AUTO, "Forza 350/ADV 350", "eSP+", "K1B/K2L"},
  {GRP_AUTO, "Giorno+/Lead 125", "4 วาล์ว eSP+", "K3M/K12"},

  {GRP_SPORT, "MSX 125", "ไฟตาเดียว (2013-2015)", "K26"},
  {GRP_SPORT, "MSX 125 SF", "ไฟสองตา (2016-2020)", "K26"},
  {GRP_SPORT, "Grom", "เกียร์ 5 สปีด (2021-ปัจจุบัน)", "K26"},
  {GRP_SPORT, "Monkey 125/CT125", "4 เกียร์ / 5 เกียร์", "K0F/K2E"},
  {GRP_SPORT, "CBR150R", "หัวฉีดโฉมแรก (2011-2018)", "KPP"},
  {GRP_SPORT, "CBR150R", "ไฟหน้า LED (2019-ปัจจุบัน)", "K94/K45"},
  {GRP_SPORT, "CB150R", "ExMotion", "K94"},
  {GRP_SPORT, "CBR250R/300R", "สูบเดียว", "KYJ/K33"},
  {GRP_SPORT, "CRF250L/300L", "วิบาก", "KZZ/K1T"},
};

static const int BIKE_COUNT = sizeof(bikeModels) / sizeof(bikeModels[0]);
#define MODEL_NONE 255
