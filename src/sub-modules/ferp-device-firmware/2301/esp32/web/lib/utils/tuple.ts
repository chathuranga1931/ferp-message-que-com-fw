/** Tuple Utilities. */
export namespace Tuple {
    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Converts a potential array (or not) into an explicit array.
     * @param items                             Items to arrayify.
     * @param append                            Optional appendors
     */
    export const arrayify = <T>(items: Utils_t.Tuple.Maybe<T> | null | undefined, ...append: T[]) =>
        items ? (Array.isArray(items) ? items : [items]) : append;
}
