/**
 * Reference tables shown on the Definitions page (static — edit here to add more).
 * Keep in sync with the firmware enums they mirror.
 */

export interface DefRow { value: number | string; name: string; note?: string }
export interface DefTable { id: string; title: string; source?: string; description?: string; rows: DefRow[] }

export const DEFINITIONS: DefTable[] = [
  {
    id: "display-type",
    title: "Display Type",
    source: "Firmware display-type enum · config key DISPLAY_TYPE (0x6001)",
    description: "Value stored in the DISPLAY_TYPE config key, selecting the pump display protocol.",
    rows: [
      { value: 0, name: "DIS_NONE" },
      { value: 1, name: "DIS_CENSTAR_6_DIGIT" },
      { value: 2, name: "DIS_CENSTAR_7_DIGIT" },
      { value: 3, name: "DIS_CENSTAR_7_DIGIT_CS" },
      { value: 4, name: "DIS_HONGYANG_8_DIGIT" },
      { value: 5, name: "DIS_WAYNE_6_DIGIT" },
      { value: 6, name: "DIS_SANKI_6_DIGIT" },
      { value: 7, name: "DIS_LONGFENG_8_DIGIT" },
      { value: 8, name: "DIS_WAYNE_6_DIGIT_2" },
      { value: 9, name: "DIS_SIZE", note: "Count of display types (not a selectable value)" },
      { value: 90, name: "DIS_RAW_8BIT_V1", note: "8-bit-per-codeword capture" },
      { value: 91, name: "DIS_RAW_12BIT_V1", note: "12-bit-per-codeword capture" },
      { value: 92, name: "DIS_RAW_SPI_V1", note: "SPI-slave + CS-timed raw byte capture" },
    ],
  },
];

/** Name for a value in a table, e.g. defName("display-type", 4) → "DIS_HONGYANG_8_DIGIT". */
export function defName(tableId: string, value: number | string): string | undefined {
  const t = DEFINITIONS.find((d) => d.id === tableId);
  return t?.rows.find((r) => String(r.value) === String(value))?.name;
}
