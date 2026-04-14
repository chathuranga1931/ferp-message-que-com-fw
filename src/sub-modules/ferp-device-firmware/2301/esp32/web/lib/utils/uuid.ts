/** Incrementing UID. */
export class UID {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Current UID. */
    private m_current = 0;

    /***********************
     *  GETTERS / SETTERS  *
     ***********************/

    /** Gets the current UID value. */
    get current() {
        return this.m_current;
    }

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /** Generates the next UID. */
    protected m_next() {
        const previous = this.m_current;
        this.m_current++;
        return previous;
    }
}

/** UUID Generators. */
export namespace UUID {
    /****************
     *  PROPERTIES  *
     ****************/

    /** UUID Lookup Table. */
    const m_LUT = Array(0xff)
        .fill('')
        .map((_, ii) => (ii < 16 ? '0' : '') + ii.toString(16));

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Creates a V4 UUID. */
    export const V4 = () => {
        var d0 = (Math.random() * 0xffffffff) | 0;
        var d1 = (Math.random() * 0xffffffff) | 0;
        var d2 = (Math.random() * 0xffffffff) | 0;
        var d3 = (Math.random() * 0xffffffff) | 0;

        return (
            m_LUT[d0 & 0xff] +
            m_LUT[(d0 >> 8) & 0xff] +
            m_LUT[(d0 >> 16) & 0xff] +
            m_LUT[(d0 >> 24) & 0xff] +
            '-' +
            m_LUT[d1 & 0xff] +
            m_LUT[(d1 >> 8) & 0xff] +
            '-' +
            m_LUT[((d1 >> 16) & 0x0f) | 0x40] +
            m_LUT[(d1 >> 24) & 0xff] +
            '-' +
            m_LUT[(d2 & 0x3f) | 0x80] +
            m_LUT[(d2 >> 8) & 0xff] +
            '-' +
            m_LUT[(d2 >> 16) & 0xff] +
            m_LUT[(d2 >> 24) & 0xff] +
            m_LUT[d3 & 0xff] +
            m_LUT[(d3 >> 8) & 0xff] +
            m_LUT[(d3 >> 16) & 0xff] +
            m_LUT[(d3 >> 24) & 0xff]
        );
    };
}
