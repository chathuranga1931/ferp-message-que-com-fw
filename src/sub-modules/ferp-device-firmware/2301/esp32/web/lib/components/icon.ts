/** Icon Components. */
export namespace Icon {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Available Icon Options. */
    export interface IOptions {
        fixedWidth?: boolean;
        size?: '2xs' | 'xs' | 'sm' | 'lg' | 'xl' | '2xl';
    }

    /***************
     *  FACTORIES  *
     ***************/

    /**
     * Constructs an icon with the given options.
     * @param name                          Name of icon.
     * @param options                       Icon options.
     */
    export const from = (name: string, options: IOptions = {}) => {
        const { fixedWidth, size } = Object.assign({}, options, { fixedWidth: true, size: 'lg' });
        return $('<i>').addClass(`fa fa-${name} fa-${size} ${fixedWidth ? 'fa-fw' : ''}`);
    };
}
