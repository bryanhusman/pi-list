import React from 'react';
import classNames from 'classnames';
import PropTypes from 'prop-types';

const propTypes = {
    className: PropTypes.string,
    size: PropTypes.oneOf(['small', 'mini', 'large', 'default']),
    inverted: PropTypes.bool
};

const defaultProps = {
    className: '',
    size: 'default',
    inverted: false
};

const Loader = (props) => {
    const spinnerClassNames = classNames(
        'lst-loader',
        props.className,
        {
            // Loader sizes
            'lst-loader--small': props.size === 'small',
            'lst-loader--mini': props.size === 'mini',
            'lst-loader--large': props.size === 'large',

            'lst-loader--inverted': props.inverted === true
        },
    );

    return (
        <span className={spinnerClassNames} />
    );
};

Loader.propTypes = propTypes;
Loader.defaultProps = defaultProps;

export default Loader;
